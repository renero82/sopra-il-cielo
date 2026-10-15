#pragma once

// Geometria locale per il radar. Header-only e senza dipendenze Arduino,
// cosi' si puo' testare sul PC (vedi test/test_geo.cpp).
//
// Entro ~150 km la proiezione equirettangolare centrata sull'osservatore
// sbaglia di pochi metri: piu' che sufficiente per un display da 240 px.

#include <math.h>

namespace geo {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kEarthRadiusKm = 6371.0f;
constexpr float kKmPerNm = 1.852f;
constexpr float kKtToKmh = 1.852f;
constexpr float kFtToM = 0.3048f;

struct Vec {
  float x;  // km verso est
  float y;  // km verso nord
};

inline Vec toLocal(float lat0, float lon0, float lat, float lon) {
  float dLon = lon - lon0;
  if (dLon > 180.0f) dLon -= 360.0f;
  if (dLon < -180.0f) dLon += 360.0f;
  const float x = dLon * kDegToRad * kEarthRadiusKm * cosf(lat0 * kDegToRad);
  const float y = (lat - lat0) * kDegToRad * kEarthRadiusKm;
  return {x, y};
}

inline float lengthKm(Vec v) { return sqrtf(v.x * v.x + v.y * v.y); }

// Rilevamento 0..360, 0 = nord, senso orario
inline float bearingDeg(Vec v) {
  float b = atan2f(v.x, v.y) * kRadToDeg;
  return b < 0.0f ? b + 360.0f : b;
}

// Sposta un punto lungo una rotta (dead reckoning tra un aggiornamento e l'altro)
inline void moveAlong(float& lat, float& lon, float trackDeg, float distKm) {
  const float t = trackDeg * kDegToRad;
  const float dNorth = cosf(t) * distKm;
  const float dEast = sinf(t) * distKm;
  const float latRad = lat * kDegToRad;
  lat += (dNorth / kEarthRadiusKm) * kRadToDeg;
  lon += (dEast / (kEarthRadiusKm * cosf(latRad))) * kRadToDeg;
  if (lon > 180.0f) lon -= 360.0f;
  if (lon < -180.0f) lon += 360.0f;
}

// Angolo sopra l'orizzonte a cui guardare (0 = orizzonte, 90 = zenit)
inline float elevationDeg(float altitudeM, float distKm) {
  if (altitudeM <= 0.0f) return 0.0f;
  return atan2f(altitudeM / 1000.0f, distKm) * kRadToDeg;
}

inline const char* compass8(float bearing) {
  static const char* const kNames[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
  int idx = static_cast<int>(floorf((bearing + 22.5f) / 45.0f)) % 8;
  if (idx < 0) idx += 8;
  return kNames[idx];
}

inline bool validLatLon(float lat, float lon) {
  return !isnan(lat) && !isnan(lon) && lat >= -90.0f && lat <= 90.0f &&
         lon >= -180.0f && lon <= 180.0f && !(lat == 0.0f && lon == 0.0f);
}

}  // namespace geo
