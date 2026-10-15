// Test sul PC della logica senza hardware: geometria e parsing JSON.
//
//   c++ -std=c++17 -Iinclude -I.pio/libdeps/cyd/ArduinoJson/src test/host_test.cpp -o host_test
//   ./host_test risposta_adsb.json [rotta_adsbdb.json]
//
// Il primo file e' una risposta di https://api.adsb.lol/v2/lat/45.4642/lon/9.19/dist/54
// (o adsb.fi), il secondo di https://api.adsbdb.com/v0/callsign/<CALLSIGN>.

#include <cstdio>
#include <fstream>
#include <sstream>

#include "geo.h"
#include "parse.h"

static int s_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FALLITO %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++s_failures;                                               \
    }                                                             \
  } while (0)
#define NEAR(a, b, tol) CHECK(fabsf((a) - (b)) <= (tol))

constexpr float kMilanoLat = 45.4642f, kMilanoLon = 9.1900f;

void testGeo() {
  // Duomo di Milano -> Malpensa: 40.45 km per 297.4 gradi (haversine)
  const geo::Vec v = geo::toLocal(kMilanoLat, kMilanoLon, 45.6306f, 8.7281f);
  NEAR(geo::lengthKm(v), 40.45f, 0.15f);
  NEAR(geo::bearingDeg(v), 297.4f, 0.5f);
  CHECK(strcmp(geo::compass8(geo::bearingDeg(v)), "O") == 0 ||
        strcmp(geo::compass8(geo::bearingDeg(v)), "NO") == 0);

  CHECK(strcmp(geo::compass8(0), "N") == 0);
  CHECK(strcmp(geo::compass8(359), "N") == 0);
  CHECK(strcmp(geo::compass8(44), "NE") == 0);
  CHECK(strcmp(geo::compass8(180), "S") == 0);
  CHECK(strcmp(geo::compass8(250), "O") == 0);

  // Dead reckoning: 12 km verso est, poi ritorno
  float lat = kMilanoLat, lon = kMilanoLon;
  geo::moveAlong(lat, lon, 90.0f, 12.0f);
  const geo::Vec e = geo::toLocal(kMilanoLat, kMilanoLon, lat, lon);
  NEAR(e.x, 12.0f, 0.05f);
  NEAR(e.y, 0.0f, 0.05f);
  geo::moveAlong(lat, lon, 270.0f, 12.0f);
  NEAR(geo::lengthKm(geo::toLocal(kMilanoLat, kMilanoLon, lat, lon)), 0.0f, 0.05f);

  // Elevazione: 10 km di quota a 10 km di distanza = 45 gradi
  NEAR(geo::elevationDeg(10000.0f, 10.0f), 45.0f, 0.01f);
  NEAR(geo::elevationDeg(10000.0f, 0.0f), 90.0f, 0.01f);

  // Antimeridiano
  const geo::Vec w = geo::toLocal(0.0f, 179.9f, 0.0f, -179.9f);
  NEAR(w.x, 22.24f, 0.1f);

  CHECK(!geo::validLatLon(NAN, 10));
  CHECK(!geo::validLatLon(0, 0));
  CHECK(geo::validLatLon(kMilanoLat, kMilanoLon));
}

void testInsertNearest() {
  Aircraft list[3];
  size_t count = 0;
  const float dists[] = {9, 3, 7, 1, 8, 2};
  for (float d : dists) {
    Aircraft a = {};
    a.distKm = d;
    parse::insertNearest(list, 3, count, a);
  }
  CHECK(count == 3);
  NEAR(list[0].distKm, 1, 0);
  NEAR(list[1].distKm, 2, 0);
  NEAR(list[2].distKm, 3, 0);
}

void testSyntheticAircraft() {
  const char* json = R"({"ac":[
    {"hex":"aaaaaa","flight":"TEST1   ","t":"A320","alt_baro":"ground","gs":12,"lat":45.4650,"lon":9.1900,"seen_pos":1},
    {"hex":"bbbbbb","flight":"TEST2","alt_geom":12000,"track":90,"lat":45.5,"lon":9.19,"seen_pos":2,"baro_rate":-1200},
    {"hex":"cccccc","flight":"NOPOS","alt_baro":30000},
    {"hex":"dddddd","flight":"STALE","alt_baro":30000,"lat":45.47,"lon":9.19,"seen_pos":120},
    {"hex":"eeeeee","alt_baro":35000,"true_heading":180.5,"lat":46.5,"lon":9.19}
  ]})";
  JsonDocument filter;
  parse::buildAircraftFilter(filter);
  JsonDocument doc;
  CHECK(!deserializeJson(doc, json, DeserializationOption::Filter(filter)));

  Aircraft out[10];
  size_t count = 0;
  const size_t total = parse::parseAircraftList(doc, kMilanoLat, kMilanoLon, 50.0f, out, 10, count);
  CHECK(total == 5);
  CHECK(count == 2);  // senza posizione, posizione vecchia e fuori raggio (~115 km) scartati
  CHECK(strcmp(out[0].hex, "aaaaaa") == 0);
  CHECK(strcmp(out[0].callsign, "TEST1") == 0);  // spazi tolti
  CHECK(out[0].onGround && !out[0].hasAlt && !out[0].hasTrack);
  CHECK(strcmp(out[1].hex, "bbbbbb") == 0);
  CHECK(out[1].hasAlt && out[1].altFt == 12000);  // ripiego su alt_geom
  CHECK(out[1].hasTrack && out[1].trackDeg == 90.0f);
  CHECK(out[1].vrateFpm == -1200);
  NEAR(out[1].bearingDeg, 0.0f, 0.1f);
  NEAR(out[1].distKm, 3.98f, 0.05f);

  // Con raggio piu' grande entra anche quello senza "track" ma con "true_heading"
  parse::parseAircraftList(doc, kMilanoLat, kMilanoLon, 200.0f, out, 10, count);
  CHECK(count == 3);
  CHECK(out[2].hasTrack && out[2].trackDeg == 180.5f);
}

bool readFile(const char* path, std::string& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

void testRealAdsb(const char* path) {
  std::string body;
  if (!readFile(path, body)) {
    std::printf("impossibile leggere %s\n", path);
    ++s_failures;
    return;
  }
  JsonDocument filter;
  parse::buildAircraftFilter(filter);
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  CHECK(!e);

  Aircraft out[cfg::kMaxAircraft];
  size_t count = 0;
  const size_t total =
      parse::parseAircraftList(doc, kMilanoLat, kMilanoLon, 100.0f, out, cfg::kMaxAircraft, count);
  std::printf("%s: %zu byte, %zu aerei nel JSON, %zu tenuti, documento filtrato %zu byte\n", path,
              body.size(), total, count, measureJson(doc));
  CHECK(total > 0);
  CHECK(count > 0);
  for (size_t i = 1; i < count; ++i) CHECK(out[i - 1].distKm <= out[i].distKm);
  for (size_t i = 0; i < count && i < 5; ++i) {
    const Aircraft& a = out[i];
    std::printf("  %-6s %-8s %-4s %6d ft %5.0f kt %5.1f km %3.0f\xC2\xB0 %s\n", a.hex, a.callsign,
                a.type, a.altFt, a.gsKt, a.distKm, a.bearingDeg, geo::compass8(a.bearingDeg));
  }
}

void testRoute(const char* path) {
  std::string body;
  if (!readFile(path, body)) return;
  JsonDocument filter;
  parse::buildRouteFilter(filter);
  JsonDocument doc;
  CHECK(!deserializeJson(doc, body, DeserializationOption::Filter(filter)));
  Route r;
  parse::parseRoute(doc, "TEST", r);
  std::printf("%s: known=%d  %s  %s (%s) -> %s (%s)\n", path, r.known, r.airline, r.origCode,
              r.origCity, r.destCode, r.destCity);
}

void testUnknownRoute() {
  JsonDocument filter;
  parse::buildRouteFilter(filter);
  JsonDocument doc;
  CHECK(!deserializeJson(doc, R"({"response":"unknown callsign"})",
                         DeserializationOption::Filter(filter)));
  Route r;
  parse::parseRoute(doc, "XXX999", r);
  CHECK(!r.known);
  CHECK(strcmp(r.callsign, "XXX999") == 0);
}

int main(int argc, char** argv) {
  testGeo();
  testInsertNearest();
  testSyntheticAircraft();
  testUnknownRoute();
  for (int i = 1; i < argc; ++i) {
    if (strstr(argv[i], "route")) {
      testRoute(argv[i]);
    } else {
      testRealAdsb(argv[i]);
    }
  }
  std::printf(s_failures ? "\n%d TEST FALLITI\n" : "\nTutti i test superati\n", s_failures);
  return s_failures ? 1 : 0;
}
