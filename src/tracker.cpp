#include "tracker.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "settings.h"

namespace tracker {

namespace {

SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_task = nullptr;

// --- Stato condiviso (accesso solo con s_mutex) ---
Aircraft s_aircraft[cfg::kMaxAircraft];
size_t s_count = 0;
uint32_t s_version = 0;
uint32_t s_fetchedMs = 0;
Status s_status = {};
bool s_relocate = true;
bool s_manual = false;
float s_manualLat = NAN;
float s_manualLon = NAN;
Route s_routes[cfg::kRouteCacheSize];
size_t s_routeNext = 0;

// Buffer di lavoro del task di rete (fuori dallo stack del task)
Aircraft s_work[cfg::kMaxAircraft];

struct Lock {
  Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mutex); }
};

const Route* findRouteLocked(const char* callsign) {
  for (const Route& r : s_routes) {
    if (r.callsign[0] && strcmp(r.callsign, callsign) == 0) return &r;
  }
  return nullptr;
}

void resolveLocation() {
  bool manual;
  float lat, lon;
  {
    Lock l;
    manual = s_manual;
    lat = s_manualLat;
    lon = s_manualLon;
    s_relocate = false;
  }

  Location loc = {};
  if (manual) {
    loc.valid = true;
    loc.lat = lat;
    loc.lon = lon;
    loc.source = LocationSource::Manual;
    strlcpy(loc.city, "Posizione manuale", sizeof(loc.city));
  } else if (api::geolocateByIp(loc)) {
    settings::saveCachedLocation(loc);
  } else if (!settings::loadCachedLocation(loc)) {
    loc.valid = true;
    loc.lat = cfg::kFallbackLat;
    loc.lon = cfg::kFallbackLon;
    loc.source = LocationSource::Fallback;
    strlcpy(loc.city, "Roma (default)", sizeof(loc.city));
  }
  Serial.printf("[loc] %.4f, %.4f  %s  (sorgente %d)\n", loc.lat, loc.lon, loc.city,
                static_cast<int>(loc.source));

  Lock l;
  s_status.loc = loc;
  s_count = 0;  // gli aerei della posizione precedente non valgono piu'
  ++s_version;
}

void fetchRoutesForNearest(size_t count) {
  uint8_t done = 0;
  for (size_t i = 0; i < count && i < 4 && done < cfg::kRouteLookupsPerPoll; ++i) {
    const Aircraft& a = s_work[i];
    if (a.onGround || a.callsign[0] == '\0') continue;
    {
      Lock l;
      if (findRouteLocked(a.callsign)) continue;
    }
    Route r;
    if (!api::fetchRoute(a.callsign, r)) continue;  // errore di rete: riprova al prossimo giro
    ++done;
    Lock l;
    s_routes[s_routeNext] = r;
    s_routeNext = (s_routeNext + 1) % cfg::kRouteCacheSize;
  }
}

void netTask(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      {
        Lock l;
        s_status.netOk = false;
        strlcpy(s_status.netErr, "Wi-Fi non connesso", sizeof(s_status.netErr));
      }
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
      continue;
    }

    bool relocate;
    {
      Lock l;
      relocate = s_relocate || !s_status.loc.valid;
    }
    if (relocate) resolveLocation();

    float lat, lon, radiusKm;
    {
      Lock l;
      lat = s_status.loc.lat;
      lon = s_status.loc.lon;
      radiusKm = cfg::kRangesKm[s_status.rangeIdx] * cfg::kQueryMarginFactor;
    }

    size_t count = 0;
    char err[48];
    const bool ok = api::fetchAircraft(lat, lon, radiusKm, s_work, cfg::kMaxAircraft, count, err,
                                       sizeof(err));
    {
      Lock l;
      // Se nel frattempo la posizione e' cambiata, questi dati vanno buttati
      const bool stale = s_relocate;
      s_status.netOk = ok;
      strlcpy(s_status.netErr, ok ? "" : err, sizeof(s_status.netErr));
      if (ok && !stale) {
        memcpy(s_aircraft, s_work, count * sizeof(Aircraft));
        s_count = count;
        s_fetchedMs = millis();
        s_status.lastFetchMs = s_fetchedMs;
        ++s_version;
      }
    }
    if (ok) fetchRoutesForNearest(count);

    Serial.printf("[adsb] %s  aerei=%u  heap=%u\n", ok ? "ok" : err, static_cast<unsigned>(count),
                  ESP.getFreeHeap());
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(cfg::kPollIntervalMs));
  }
}

void wake() {
  if (s_task) xTaskNotifyGive(s_task);
}

}  // namespace

void begin(bool manualLocation, float lat, float lon, uint8_t rangeIdx) {
  s_mutex = xSemaphoreCreateMutex();
  s_manual = manualLocation;
  s_manualLat = lat;
  s_manualLon = lon;
  s_status.rangeIdx = rangeIdx;
  strlcpy(s_status.netErr, "In attesa...", sizeof(s_status.netErr));
  // 16 KB di stack: l'handshake TLS di mbedTLS e' il consumatore principale
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, &s_task, 0);
}

bool copyIfNewer(uint32_t& version, Aircraft* out, size_t& count, uint32_t& fetchedMs) {
  Lock l;
  if (version == s_version) return false;
  memcpy(out, s_aircraft, s_count * sizeof(Aircraft));
  count = s_count;
  fetchedMs = s_fetchedMs;
  version = s_version;
  return true;
}

void getStatus(Status& out) {
  Lock l;
  out = s_status;
}

bool lookupRoute(const char* callsign, Route& out) {
  if (!callsign || !callsign[0]) return false;
  Lock l;
  const Route* r = findRouteLocked(callsign);
  if (!r) return false;
  out = *r;
  return true;
}

void setRangeIdx(uint8_t idx) {
  {
    Lock l;
    s_status.rangeIdx = idx;
  }
  wake();
}

void setLocation(bool manual, float lat, float lon) {
  {
    Lock l;
    s_manual = manual;
    s_manualLat = lat;
    s_manualLon = lon;
    s_relocate = true;
  }
  wake();
}

}  // namespace tracker
