#include "api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <string.h>

#include "config.h"
#include "geo.h"
#include "parse.h"

namespace api {

namespace {

constexpr char kUserAgent[] = "SkyRadar/1.0 (ESP32; +https://github.com)";

struct AdsbSource {
  const char* name;
  const char* urlFormat;  // lat, lon, raggio in miglia nautiche
};

// Entrambe espongono l'API "v2" compatibile readsb/ADSBexchange.
// adsb.lol risponde con la chiave "ac", adsb.fi con "aircraft": il filtro le accetta tutte e due.
constexpr AdsbSource kAdsbSources[] = {
    {"adsb.lol", "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d"},
    {"adsb.fi", "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d"},
};

// Un client per richiesta: con HTTP/1.0 il server chiude la connessione e
// manda Content-Length, cosi' possiamo fare parsing in streaming senza
// bufferizzare l'intera risposta (che in zone trafficate supera i 50 KB).
class HttpGet {
 public:
  explicit HttpGet(const char* url) {
    const bool tls = strncmp(url, "https://", 8) == 0;
    if (tls) {
      // Niente verifica del certificato: i dati sono pubblici e in sola lettura,
      // e cosi' evitiamo di dover aggiornare il firmware quando scadono le CA.
      secure_.setInsecure();
    }
    http_.useHTTP10(true);
    http_.setReuse(false);
    http_.setTimeout(cfg::kHttpTimeoutMs);
    http_.setConnectTimeout(6000);
    http_.setUserAgent(kUserAgent);
    ok_ = tls ? http_.begin(secure_, url) : http_.begin(plain_, url);
    if (ok_) http_.addHeader("Accept-Encoding", "identity");
  }
  ~HttpGet() { http_.end(); }

  int get() { return ok_ ? http_.GET() : HTTPC_ERROR_CONNECTION_REFUSED; }
  Stream& stream() { return http_.getStream(); }

 private:
  WiFiClient plain_;
  WiFiClientSecure secure_;
  HTTPClient http_;
  bool ok_ = false;
};

bool fetchFrom(const AdsbSource& src, float lat, float lon, float radiusKm, Aircraft* out,
               size_t maxCount, size_t& count, char* err, size_t errLen) {
  int nm = static_cast<int>(ceilf(radiusKm / geo::kKmPerNm));
  if (nm < 1) nm = 1;
  if (nm > 250) nm = 250;  // limite delle API

  char url[128];
  snprintf(url, sizeof(url), src.urlFormat, lat, lon, nm);

  HttpGet req(url);
  const int code = req.get();
  if (code != 200) {
    if (code > 0) {
      snprintf(err, errLen, "%s: HTTP %d", src.name, code);
    } else {
      snprintf(err, errLen, "%s: %s", src.name, HTTPClient::errorToString(code).c_str());
    }
    return false;
  }

  JsonDocument filter;
  parse::buildAircraftFilter(filter);
  JsonDocument doc;
  const DeserializationError e =
      deserializeJson(doc, req.stream(), DeserializationOption::Filter(filter));
  if (e) {
    snprintf(err, errLen, "%s: JSON %s", src.name, e.c_str());
    return false;
  }
  parse::parseAircraftList(doc, lat, lon, radiusKm, out, maxCount, count);
  err[0] = '\0';
  return true;
}

}  // namespace

bool geolocateByIp(Location& out) {
  struct Provider {
    const char* url;
    const char* okKey;
    const char* latKey;
    const char* lonKey;
  };
  // ip-api.com gratuito e' solo HTTP: per una posizione approssimata va bene.
  static const Provider kProviders[] = {
      {"http://ip-api.com/json/?fields=status,lat,lon,city", "status", "lat", "lon"},
      {"https://ipwho.is/?fields=success,latitude,longitude,city", "success", "latitude", "longitude"},
  };

  for (const Provider& pr : kProviders) {
    HttpGet req(pr.url);
    if (req.get() != 200) continue;
    JsonDocument doc;
    if (deserializeJson(doc, req.stream())) continue;

    JsonVariantConst ok = doc[pr.okKey];
    const bool success = ok.is<bool>() ? ok.as<bool>()
                                       : (ok.is<const char*>() &&
                                          strcmp(ok.as<const char*>(), "success") == 0);
    const float lat = doc[pr.latKey] | NAN;
    const float lon = doc[pr.lonKey] | NAN;
    if (!success || !geo::validLatLon(lat, lon)) continue;

    out.valid = true;
    out.lat = lat;
    out.lon = lon;
    strlcpy(out.city, doc["city"] | "", sizeof(out.city));
    out.source = LocationSource::Ip;
    return true;
  }
  return false;
}

bool fetchAircraft(float lat, float lon, float radiusKm, Aircraft* out, size_t maxCount,
                   size_t& count, char* err, size_t errLen) {
  for (const AdsbSource& src : kAdsbSources) {
    if (fetchFrom(src, lat, lon, radiusKm, out, maxCount, count, err, errLen)) return true;
    Serial.printf("[adsb] %s\n", err);
  }
  return false;
}

bool fetchRoute(const char* callsign, Route& out) {
  memset(&out, 0, sizeof(out));
  strlcpy(out.callsign, callsign, sizeof(out.callsign));

  char safe[9];
  size_t n = 0;
  for (const char* c = callsign; *c && n < sizeof(safe) - 1; ++c) {
    if (isalnum(static_cast<unsigned char>(*c))) safe[n++] = *c;
  }
  safe[n] = '\0';
  if (n < 3) return true;  // callsign assente o non valido: niente da cercare

  char url[80];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", safe);
  HttpGet req(url);
  const int code = req.get();
  if (code == 404) return true;  // volo non in archivio (privati, militari, ...)
  if (code != 200) return false;

  JsonDocument filter;
  parse::buildRouteFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, req.stream(), DeserializationOption::Filter(filter))) return false;
  parse::parseRoute(doc, callsign, out);
  return true;
}

}  // namespace api
