#include "settings.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "geo.h"

namespace settings {

namespace {
constexpr char kNs[] = "skyradar";
}

void load(Settings& out) {
  Preferences p;
  p.begin(kNs, true);
  out.manualLocation = p.getBool("manual", false);
  out.lat = p.getFloat("lat", NAN);
  out.lon = p.getFloat("lon", NAN);
  out.rangeIdx = p.getUChar("range", cfg::kDefaultRangeIdx);
  out.overheadKm = p.getFloat("ovh", cfg::kDefaultOverheadKm);
  p.end();

  if (out.rangeIdx >= cfg::kRangeCount) out.rangeIdx = cfg::kDefaultRangeIdx;
  if (!(out.overheadKm > 0.1f && out.overheadKm < 50.0f)) out.overheadKm = cfg::kDefaultOverheadKm;
  if (out.manualLocation && !geo::validLatLon(out.lat, out.lon)) out.manualLocation = false;
}

void saveManualLocation(bool manual, float lat, float lon) {
  Preferences p;
  p.begin(kNs, false);
  p.putBool("manual", manual);
  p.putFloat("lat", lat);
  p.putFloat("lon", lon);
  p.end();
}

void saveRangeIdx(uint8_t idx) {
  Preferences p;
  p.begin(kNs, false);
  p.putUChar("range", idx);
  p.end();
}

void saveOverheadKm(float km) {
  Preferences p;
  p.begin(kNs, false);
  p.putFloat("ovh", km);
  p.end();
}

void saveCachedLocation(const Location& loc) {
  Preferences p;
  p.begin(kNs, false);
  p.putFloat("c_lat", loc.lat);
  p.putFloat("c_lon", loc.lon);
  p.putString("c_city", loc.city);
  p.end();
}

bool loadCachedLocation(Location& out) {
  Preferences p;
  p.begin(kNs, true);
  const float lat = p.getFloat("c_lat", NAN);
  const float lon = p.getFloat("c_lon", NAN);
  String city = p.getString("c_city", "");
  p.end();
  if (!geo::validLatLon(lat, lon)) return false;
  out.valid = true;
  out.lat = lat;
  out.lon = lon;
  strlcpy(out.city, city.c_str(), sizeof(out.city));
  out.source = LocationSource::Cached;
  return true;
}

void clearAll() {
  Preferences p;
  p.begin(kNs, false);
  p.clear();
  p.end();
}

}  // namespace settings
