#pragma once

// Parsing delle risposte JSON (adsb.lol / adsb.fi / adsbdb).
// Solo ArduinoJson + C standard: compila anche sul PC per i test (test/host_test.cpp).

#include <ArduinoJson.h>
#include <string.h>

#include "config.h"
#include "geo.h"
#include "model.h"

namespace parse {

// Copia una stringa togliendo gli spazi (i callsign arrivano come "RYR12AB ")
inline void copyTrimmed(char* dst, size_t dstLen, const char* src) {
  if (dstLen == 0) return;
  dst[0] = '\0';
  if (!src) return;
  while (*src == ' ') ++src;
  strlcpy(dst, src, dstLen);
  size_t n = strlen(dst);
  while (n > 0 && dst[n - 1] == ' ') dst[--n] = '\0';
}

inline bool readFloat(JsonVariantConst v, float& out) {
  if (!v.is<float>()) return false;
  out = v.as<float>();
  return true;
}

// Inserisce mantenendo l'array ordinato per distanza e lungo al massimo maxCount
inline void insertNearest(Aircraft* list, size_t maxCount, size_t& count, const Aircraft& a) {
  size_t i;
  if (count < maxCount) {
    i = count++;
  } else if (a.distKm < list[maxCount - 1].distKm) {
    i = maxCount - 1;
  } else {
    return;
  }
  while (i > 0 && list[i - 1].distKm > a.distKm) {
    list[i] = list[i - 1];
    --i;
  }
  list[i] = a;
}

// Teniamo solo i campi che ci servono: il JsonDocument resta piccolo anche con 200 aerei
inline void buildAircraftFilter(JsonDocument& filter) {
  for (const char* key : {"ac", "aircraft"}) {  // adsb.lol usa "ac", adsb.fi "aircraft"
    JsonObject f = filter[key].add<JsonObject>();
    for (const char* field : {"hex", "flight", "r", "t", "alt_baro", "alt_geom", "gs", "track",
                              "true_heading", "baro_rate", "geom_rate", "lat", "lon", "seen_pos"}) {
      f[field] = true;
    }
  }
}

inline bool parseAircraft(JsonObjectConst p, float lat0, float lon0, Aircraft& a) {
  float lat, lon;
  if (!readFloat(p["lat"], lat) || !readFloat(p["lon"], lon)) return false;

  memset(&a, 0, sizeof(a));
  a.seenPosS = p["seen_pos"] | 0.0f;
  if (a.seenPosS > cfg::kMaxSeenPosS) return false;

  a.lat = lat;
  a.lon = lon;
  copyTrimmed(a.hex, sizeof(a.hex), p["hex"] | "");
  copyTrimmed(a.callsign, sizeof(a.callsign), p["flight"] | "");
  copyTrimmed(a.reg, sizeof(a.reg), p["r"] | "");
  copyTrimmed(a.type, sizeof(a.type), p["t"] | "");

  JsonVariantConst alt = p["alt_baro"];
  if (alt.is<const char*>()) {
    a.onGround = strcmp(alt.as<const char*>(), "ground") == 0;
  } else if (alt.is<float>()) {
    a.altFt = alt.as<int32_t>();
    a.hasAlt = true;
  } else if (p["alt_geom"].is<float>()) {
    a.altFt = p["alt_geom"].as<int32_t>();
    a.hasAlt = true;
  }

  a.gsKt = p["gs"] | 0.0f;
  float track;
  if (readFloat(p["track"], track) || readFloat(p["true_heading"], track)) {
    a.trackDeg = track;
    a.hasTrack = true;
  }
  a.vrateFpm = static_cast<int16_t>(p["baro_rate"] | (p["geom_rate"] | 0));

  const geo::Vec v = geo::toLocal(lat0, lon0, a.lat, a.lon);
  a.distKm = geo::lengthKm(v);
  a.bearingDeg = geo::bearingDeg(v);
  return true;
}

// Riempie `out` con gli aerei entro radiusKm, dal piu' vicino. Ritorna quanti ne ha letti in totale.
inline size_t parseAircraftList(const JsonDocument& doc, float lat0, float lon0, float radiusKm,
                                Aircraft* out, size_t maxCount, size_t& count) {
  JsonArrayConst arr = doc["ac"].is<JsonArrayConst>() ? doc["ac"].as<JsonArrayConst>()
                                                      : doc["aircraft"].as<JsonArrayConst>();
  count = 0;
  size_t total = 0;
  for (JsonObjectConst p : arr) {
    ++total;
    Aircraft a;
    if (parseAircraft(p, lat0, lon0, a) && a.distKm <= radiusKm) {
      insertNearest(out, maxCount, count, a);
    }
  }
  return total;
}

inline void buildRouteFilter(JsonDocument& filter) {
  JsonObject fr = filter["response"]["flightroute"].to<JsonObject>();
  fr["airline"]["name"] = true;
  for (const char* end : {"origin", "destination"}) {
    fr[end]["iata_code"] = true;
    fr[end]["icao_code"] = true;
    fr[end]["municipality"] = true;
  }
}

// Ritorna la rotta; route.known = false se adsbdb non conosce il volo
inline void parseRoute(const JsonDocument& doc, const char* callsign, Route& out) {
  memset(&out, 0, sizeof(out));
  strlcpy(out.callsign, callsign, sizeof(out.callsign));
  JsonObjectConst route = doc["response"]["flightroute"];
  if (route.isNull()) return;

  JsonObjectConst o = route["origin"];
  JsonObjectConst d = route["destination"];
  out.known = true;
  strlcpy(out.airline, route["airline"]["name"] | "", sizeof(out.airline));
  strlcpy(out.origCode, o["iata_code"] | (o["icao_code"] | "?"), sizeof(out.origCode));
  strlcpy(out.origCity, o["municipality"] | "", sizeof(out.origCity));
  strlcpy(out.destCode, d["iata_code"] | (d["icao_code"] | "?"), sizeof(out.destCode));
  strlcpy(out.destCity, d["municipality"] | "", sizeof(out.destCity));
}

}  // namespace parse
