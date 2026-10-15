#include "ui.h"

#include <TFT_eSPI.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "geo.h"

namespace ui {

namespace {

TFT_eSPI tft;
TFT_eSprite spr(&tft);
// Si disegna tutto su uno sprite 320x240 a 8 bit (75 KB) e lo si invia in un
// colpo solo: niente sfarfallio. Se la RAM non basta si disegna direttamente.
TFT_eSPI* g = &tft;
bool s_useSprite = false;

constexpr int kW = 320;
constexpr int kH = 240;
constexpr int kCx = 120;  // centro radar
constexpr int kCy = 120;
constexpr int kR = 112;   // raggio radar in pixel
constexpr int kPanelX = 240;

// Palette pensata per RGB332 (lo sprite a 8 bit ha 8 livelli R/G e 4 di B)
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t kBlack = rgb(0, 0, 0);
constexpr uint16_t kWhite = rgb(255, 255, 255);
constexpr uint16_t kGrey = rgb(146, 146, 170);
constexpr uint16_t kDimGrey = rgb(73, 73, 85);
constexpr uint16_t kRing = rgb(0, 109, 0);
constexpr uint16_t kGrid = rgb(0, 73, 0);
constexpr uint16_t kPanel = rgb(0, 36, 85);
constexpr uint16_t kHeader = rgb(0, 0, 85);
constexpr uint16_t kCyan = rgb(0, 219, 255);
constexpr uint16_t kYellow = rgb(255, 219, 0);
constexpr uint16_t kOrange = rgb(255, 146, 0);
constexpr uint16_t kRed = rgb(219, 0, 0);
constexpr uint16_t kDarkRed = rgb(109, 0, 0);
constexpr uint16_t kGreen = rgb(0, 255, 0);
constexpr uint16_t kOverheadRing = rgb(146, 109, 0);

// ---------------------------------------------------------------------------
// Colori per quota, stile FR24/tar1090: arancio basso -> verde -> blu -> viola alto
uint16_t hsv(float h, float s, float v) {
  h = fmodf(h, 360.0f) / 60.0f;
  const int i = static_cast<int>(h);
  const float f = h - i;
  const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  float r, gg, b;
  switch (i) {
    case 0: r = v; gg = t; b = p; break;
    case 1: r = q; gg = v; b = p; break;
    case 2: r = p; gg = v; b = t; break;
    case 3: r = p; gg = q; b = v; break;
    case 4: r = t; gg = p; b = v; break;
    default: r = v; gg = p; b = q; break;
  }
  return rgb(r * 255, gg * 255, b * 255);
}

uint16_t altColor(const Aircraft& a, float value = 1.0f) {
  if (a.onGround || !a.hasAlt) return value < 1.0f ? kDimGrey : kGrey;
  static const float kAlt[] = {0, 2000, 6000, 10000, 20000, 30000, 40000};
  static const float kHue[] = {20, 35, 60, 120, 180, 230, 290};
  const float alt = constrain(static_cast<float>(a.altFt), 0.0f, 40000.0f);
  float hue = kHue[6];
  for (int i = 0; i < 6; ++i) {
    if (alt <= kAlt[i + 1]) {
      hue = kHue[i] + (kHue[i + 1] - kHue[i]) * (alt - kAlt[i]) / (kAlt[i + 1] - kAlt[i]);
      break;
    }
  }
  return hsv(hue, 1.0f, value);
}

// ---------------------------------------------------------------------------
// Testo e piccole primitive

void text(const char* s, int x, int y, uint8_t font, uint16_t color, uint8_t datum = TL_DATUM) {
  g->setTextColor(color);
  g->setTextDatum(datum);
  g->drawString(s, x, y, font);
}

// I font integrati non hanno il simbolo dei gradi: lo disegniamo a mano
void textDeg(const char* s, int x, int y, uint8_t font, uint16_t color) {
  text(s, x, y, font, color, TL_DATUM);
  const int w = g->textWidth(s, font);
  const int r = font == 1 ? 1 : 2;
  g->drawCircle(x + w + r + 1, y + r + 1, r, color);
}

void fmtAltitude(char* out, size_t n, const Aircraft& a) {
  if (a.onGround) {
    strlcpy(out, "a terra", n);
  } else if (!a.hasAlt) {
    strlcpy(out, "-", n);
  } else {
    snprintf(out, n, "%ld m", lroundf(a.altFt * geo::kFtToM));
  }
}

void fmtDistance(char* out, size_t n, float km) {
  if (km < 10.0f) {
    snprintf(out, n, "%.1f", km);
  } else {
    snprintf(out, n, "%d", static_cast<int>(lroundf(km)));
  }
}

void fitCopy(char* out, size_t n, const char* s, uint8_t font, int maxW) {
  if (out != s) strlcpy(out, s, n);
  size_t len = strlen(out);
  while (len > 0 && g->textWidth(out, font) > maxW) out[--len] = '\0';
}

void drawPlaneIcon(int x, int y, float trackDeg, uint16_t color, float len) {
  const float t = trackDeg * geo::kDegToRad;
  auto pt = [&](float angle, float l, int& px, int& py) {
    px = x + static_cast<int>(lroundf(sinf(angle) * l));
    py = y - static_cast<int>(lroundf(cosf(angle) * l));
  };
  int nx, ny, lx, ly, rx, ry, bx, by;
  pt(t, len, nx, ny);
  pt(t + 2.45f, len * 0.9f, lx, ly);
  pt(t - 2.45f, len * 0.9f, rx, ry);
  pt(t + geo::kPi, len * 0.3f, bx, by);
  g->fillTriangle(nx, ny, lx, ly, bx, by, color);
  g->fillTriangle(nx, ny, rx, ry, bx, by, color);
}

void drawVrateArrow(int x, int y, int16_t fpm) {
  if (fpm > 300) {
    g->fillTriangle(x, y - 5, x - 4, y + 3, x + 4, y + 3, kGreen);
  } else if (fpm < -300) {
    g->fillTriangle(x, y + 5, x - 4, y - 3, x + 4, y - 3, kOrange);
  }
}

void drawToast(const char* msg) {
  const int w = g->textWidth(msg, 4) + 24;
  const int x = (kW - w) / 2;
  g->fillRoundRect(x, 96, w, 44, 8, kHeader);
  g->drawRoundRect(x, 96, w, 44, 8, kCyan);
  text(msg, kW / 2, 118, 4, kWhite, MC_DATUM);
}

const char* sourceLabel(LocationSource s) {
  switch (s) {
    case LocationSource::Manual: return "manuale";
    case LocationSource::Ip: return "da IP";
    case LocationSource::Cached: return "da cache";
    case LocationSource::Fallback: return "predefinita";
    default: return "";
  }
}

void fmtStatus(char* out, size_t n, const Frame& f) {
  const tracker::Status& st = *f.status;
  if (!st.loc.valid) {
    strlcpy(out, "Localizzo...", n);
  } else if (st.lastFetchMs == 0) {
    strlcpy(out, st.netErr[0] ? st.netErr : "Scarico dati...", n);
  } else {
    const uint32_t age = (f.nowMs - st.lastFetchMs) / 1000;
    snprintf(out, n, st.netOk ? "agg. %lus fa" : "ERR %lus", static_cast<unsigned long>(age));
  }
}

// ---------------------------------------------------------------------------
// Scie: ultime posizioni ricevute per ogni aereo

struct Trail {
  char hex[7];
  float lat[cfg::kTrailPoints];
  float lon[cfg::kTrailPoints];
  uint8_t n;
  uint8_t missed;  // aggiornamenti consecutivi in cui l'aereo non c'era
};
Trail s_trails[cfg::kMaxAircraft + 8];

Trail* findTrail(const char* hex) {
  for (Trail& t : s_trails) {
    if (t.n && strcmp(t.hex, hex) == 0) return &t;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Vista RADAR

void radarPoint(float distKm, float bearingDeg, float scale, int& px, int& py) {
  const float b = bearingDeg * geo::kDegToRad;
  px = kCx + static_cast<int>(lroundf(sinf(b) * distKm * scale));
  py = kCy - static_cast<int>(lroundf(cosf(b) * distKm * scale));
}

void drawRadarGrid(const Frame& f, float scale) {
  g->drawLine(kCx - kR, kCy, kCx + kR, kCy, kGrid);
  g->drawLine(kCx, kCy - kR, kCx, kCy + kR, kGrid);
  for (int i = 1; i <= 3; ++i) g->drawCircle(kCx, kCy, kR * i / 3, kRing);

  // Cerchio "sopra di te" tratteggiato
  const float rOv = f.overheadKm * scale;
  if (rOv >= 4.0f) {
    for (int deg = 0; deg < 360; deg += 12) {
      const float a = deg * geo::kDegToRad;
      g->drawPixel(kCx + lroundf(sinf(a) * rOv), kCy - lroundf(cosf(a) * rOv), kOverheadRing);
    }
  }

  text("N", kCx, kCy - kR + 3, 2, kWhite, TC_DATUM);
  text("S", kCx, kCy + kR - 3, 2, kGrey, BC_DATUM);
  text("E", kCx + kR - 4, kCy, 2, kGrey, MR_DATUM);
  text("O", kCx - kR + 4, kCy, 2, kGrey, ML_DATUM);

  // Etichette distanza lungo la diagonale NE
  char buf[12];
  for (int i = 1; i <= 3; ++i) {
    const float km = f.rangeKm * i / 3.0f;
    if (i == 3) {
      snprintf(buf, sizeof(buf), "%dkm", static_cast<int>(lroundf(km)));
    } else {
      fmtDistance(buf, sizeof(buf), km);
    }
    const int r = kR * i / 3;
    text(buf, kCx + lroundf(r * 0.707f) + 2, kCy - lroundf(r * 0.707f) - 2, 1, kRing, BL_DATUM);
  }
}

void drawSweep(uint32_t now) {
  const float a0 = (now % cfg::kSweepPeriodMs) * 360.0f / cfg::kSweepPeriodMs;
  for (int k = 7; k >= 0; --k) {
    const float a = (a0 - k * 2.5f) * geo::kDegToRad;
    const uint16_t c = rgb(0, 255 - k * 30, 0);
    g->drawLine(kCx, kCy, kCx + lroundf(sinf(a) * kR), kCy - lroundf(cosf(a) * kR), c);
  }
}

void drawTrail(const Aircraft& a, const Location& loc, float scale) {
  const Trail* t = findTrail(a.hex);
  if (!t || t->n == 0) return;
  const uint16_t c = altColor(a, 0.5f);
  int px = 0, py = 0;
  for (uint8_t i = 0; i <= t->n; ++i) {
    geo::Vec v;
    if (i < t->n) {
      v = geo::toLocal(loc.lat, loc.lon, t->lat[i], t->lon[i]);
    } else {
      v = geo::toLocal(loc.lat, loc.lon, a.lat, a.lon);  // fino alla posizione attuale
    }
    int x, y;
    radarPoint(geo::lengthKm(v), geo::bearingDeg(v), scale, x, y);
    if (i > 0) g->drawLine(px, py, x, y, c);
    px = x;
    py = y;
  }
}

void drawSidePanel(const Frame& f) {
  g->fillRect(kPanelX, 0, kW - kPanelX, kH, kPanel);
  const int cx = kPanelX + (kW - kPanelX) / 2;
  const int lx = kPanelX + 5;
  char buf[40];

  text("SOPRA IL", cx, 3, 1, kCyan, TC_DATUM);
  text("CIELO", cx, 12, 1, kCyan, TC_DATUM);

  struct tm tm;
  if (getLocalTime(&tm, 0)) {
    strftime(buf, sizeof(buf), "%H:%M", &tm);
  } else {
    strlcpy(buf, "--:--", sizeof(buf));
  }
  text(buf, cx, 22, 4, kWhite, TC_DATUM);

  const Location& loc = f.status->loc;
  fitCopy(buf, sizeof(buf), loc.valid ? loc.city : "...", 1, 72);
  text(buf, cx, 52, 1, kWhite, TC_DATUM);
  text(sourceLabel(loc.source), cx, 62, 1, kGrey, TC_DATUM);

  size_t inRange = 0;
  for (size_t i = 0; i < f.count; ++i) inRange += f.ac[i].distKm <= f.rangeKm;
  snprintf(buf, sizeof(buf), "Aerei %u", static_cast<unsigned>(inRange));
  text(buf, lx, 76, 2, kWhite);
  snprintf(buf, sizeof(buf), "Raggio %ukm", static_cast<unsigned>(f.rangeKm));
  text(buf, lx, 94, 1, kGrey);

  g->drawFastHLine(kPanelX + 4, 106, kW - kPanelX - 8, kGrey);

  if (f.focus >= 0) {
    const Aircraft& a = f.ac[f.focus];
    text(f.overhead ? "SOPRA DI TE" : "PIU' VICINO", lx, 112, 1,
         f.overhead ? kYellow : kGrey);
    text(a.callsign[0] ? a.callsign : a.hex, lx, 124, 2, altColor(a));
    snprintf(buf, sizeof(buf), "%s %s", a.type[0] ? a.type : "?", a.reg);
    fitCopy(buf, sizeof(buf), buf, 1, 72);
    text(buf, lx, 142, 1, kWhite);
    fmtAltitude(buf, sizeof(buf), a);
    text(buf, lx, 154, 1, kWhite);
    char d[12];
    fmtDistance(d, sizeof(d), a.distKm);
    snprintf(buf, sizeof(buf), "%s km %s", d, geo::compass8(a.bearingDeg));
    text(buf, lx, 166, 1, kWhite);
    if (a.hasAlt && !a.onGround) {
      snprintf(buf, sizeof(buf), "alto %d",
               static_cast<int>(geo::elevationDeg(a.altFt * geo::kFtToM, a.distKm)));
      textDeg(buf, lx, 178, 1, kCyan);
    }
    if (f.route && f.route->known) {
      snprintf(buf, sizeof(buf), "%s>%s", f.route->origCode, f.route->destCode);
      fitCopy(buf, sizeof(buf), buf, 2, 72);
      text(buf, lx, 192, 2, kYellow);
    }
  } else {
    text("Cielo libero", lx, 118, 1, kGrey);
  }

  fmtStatus(buf, sizeof(buf), f);
  fitCopy(buf, sizeof(buf), buf, 1, 74);
  text(buf, cx, kH - 4, 1, f.status->netOk ? kGreen : kRed, BC_DATUM);
}

void renderRadar(const Frame& f) {
  g->fillScreen(kBlack);
  const float scale = kR / f.rangeKm;
  drawRadarGrid(f, scale);
  drawSweep(f.nowMs);

  const Location& loc = f.status->loc;
  for (size_t i = 0; i < f.count; ++i) {
    if (f.ac[i].distKm <= f.rangeKm) drawTrail(f.ac[i], loc, scale);
  }

  // Dal piu' lontano al piu' vicino: quelli vicini restano sopra
  for (int i = static_cast<int>(f.count) - 1; i >= 0; --i) {
    const Aircraft& a = f.ac[i];
    if (a.distKm > f.rangeKm) continue;
    int x, y;
    radarPoint(a.distKm, a.bearingDeg, scale, x, y);
    const uint16_t c = altColor(a);
    const bool isFocus = i == f.focus;
    if (a.hasTrack) {
      drawPlaneIcon(x, y, a.trackDeg, c, isFocus ? 9.0f : 6.5f);
    } else {
      g->fillRect(x - 2, y - 2, 5, 5, c);
    }
    if (isFocus) {
      const int pulse = f.overhead ? static_cast<int>((f.nowMs / 80) % 8) : 0;
      g->drawCircle(x, y, 12 + pulse, f.overhead ? kYellow : kWhite);
    }
    if (i < cfg::kMaxLabels) {
      const char* label = a.callsign[0] ? a.callsign : a.hex;
      if (x > kCx + 50) {
        text(label, x - 9, y - 9, 1, c, TR_DATUM);
      } else {
        text(label, x + 9, y - 9, 1, c, TL_DATUM);
      }
    }
  }

  // Tu sei qui
  g->drawCircle(kCx, kCy, 3, kWhite);
  g->drawPixel(kCx, kCy, kWhite);

  drawSidePanel(f);
}

// ---------------------------------------------------------------------------
// Vista LISTA

void renderList(const Frame& f) {
  g->fillScreen(kBlack);
  g->fillRect(0, 0, kW, 22, kHeader);
  text("VOLI VICINI", 6, 11, 2, kCyan, ML_DATUM);
  char buf[48];
  snprintf(buf, sizeof(buf), "raggio %u km", static_cast<unsigned>(f.rangeKm));
  text(buf, kW - 6, 11, 2, kWhite, MR_DATUM);

  const int y0 = 26;
  text("VOLO", 4, y0, 1, kGrey);
  text("TIPO", 76, y0, 1, kGrey);
  text("QUOTA", 168, y0, 1, kGrey, TR_DATUM);
  text("KM/H", 214, y0, 1, kGrey, TR_DATUM);
  text("DIST KM", 262, y0, 1, kGrey, TR_DATUM);

  int row = 0;
  for (size_t i = 0; i < f.count && row < 9; ++i) {
    const Aircraft& a = f.ac[i];
    if (a.distKm > f.rangeKm) continue;
    const int y = 38 + row * 21;
    if (static_cast<int>(i) == f.focus && f.overhead) g->fillRect(0, y - 1, kW, 20, kDarkRed);

    text(a.callsign[0] ? a.callsign : a.hex, 4, y + 1, 2, altColor(a));
    text(a.type[0] ? a.type : "-", 76, y + 1, 2, kWhite);
    fmtAltitude(buf, sizeof(buf), a);
    text(buf, 168, y + 1, 2, kWhite, TR_DATUM);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(a.gsKt * geo::kKtToKmh)));
    text(buf, 214, y + 1, 2, kWhite, TR_DATUM);
    fmtDistance(buf, sizeof(buf), a.distKm);
    text(buf, 262, y + 1, 2, kWhite, TR_DATUM);
    text(geo::compass8(a.bearingDeg), 268, y + 5, 1, kGrey);
    drawVrateArrow(306, y + 8, a.vrateFpm);
    ++row;
  }
  if (row == 0) text("Nessun aereo nel raggio", kW / 2, 120, 2, kGrey, MC_DATUM);

  fmtStatus(buf, sizeof(buf), f);
  text(buf, kW - 4, kH - 2, 1, f.status->netOk ? kGreen : kRed, BR_DATUM);
  fitCopy(buf, sizeof(buf), f.status->loc.city, 1, 180);
  text(buf, 4, kH - 2, 1, kGrey, BL_DATUM);
}

// ---------------------------------------------------------------------------
// Vista DETTAGLIO (il volo piu' vicino o quello sopra di te)

void drawLookCompass(const Aircraft& a, int cx, int cy, int r) {
  g->drawCircle(cx, cy, r, kRing);
  g->drawCircle(cx, cy, r - 1, kGrid);
  for (int deg = 0; deg < 360; deg += 45) {
    const float t = deg * geo::kDegToRad;
    g->drawLine(cx + lroundf(sinf(t) * (r - 5)), cy - lroundf(cosf(t) * (r - 5)),
                cx + lroundf(sinf(t) * r), cy - lroundf(cosf(t) * r), kRing);
  }
  text("N", cx, cy - r - 2, 1, kWhite, BC_DATUM);

  // La freccia indica dove guardare; piu' l'aereo e' alto nel cielo, piu' e' corta
  const float elev = a.hasAlt ? geo::elevationDeg(a.altFt * geo::kFtToM, a.distKm) : 0.0f;
  const float len = (r - 8) * (1.0f - elev / 90.0f) + 4.0f;
  const float b = a.bearingDeg * geo::kDegToRad;
  const int tx = cx + lroundf(sinf(b) * len);
  const int ty = cy - lroundf(cosf(b) * len);
  g->drawLine(cx, cy, tx, ty, kYellow);
  g->drawLine(cx + 1, cy, tx + 1, ty, kYellow);
  drawPlaneIcon(tx, ty, a.hasTrack ? a.trackDeg : a.bearingDeg, altColor(a), 8.0f);
  g->fillCircle(cx, cy, 2, kWhite);

  char buf[24];
  snprintf(buf, sizeof(buf), "GUARDA A %s", geo::compass8(a.bearingDeg));
  text(buf, cx, cy + r + 8, 1, kWhite, TC_DATUM);
  snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(elev)));
  const int w = g->textWidth(buf, 2) + g->textWidth(" sull'orizz.", 1) + 6;
  const int x0 = cx - w / 2;
  textDeg(buf, x0, cy + r + 20, 2, kCyan);
  text(" sull'orizz.", x0 + g->textWidth(buf, 2) + 6, cy + r + 25, 1, kCyan);
}

void renderDetail(const Frame& f) {
  g->fillScreen(kBlack);
  char buf[48];

  if (f.focus < 0) {
    g->fillRect(0, 0, kW, 26, kHeader);
    text("DETTAGLIO VOLO", kW / 2, 13, 2, kCyan, MC_DATUM);
    snprintf(buf, sizeof(buf), "Nessun aereo entro %u km", static_cast<unsigned>(f.rangeKm));
    text(buf, kW / 2, 120, 2, kGrey, MC_DATUM);
    return;
  }

  const Aircraft& a = f.ac[f.focus];
  if (f.overhead) {
    const bool blink = (f.nowMs / 500) % 2;
    g->fillRect(0, 0, kW, 26, blink ? kRed : kDarkRed);
    text("SOPRA DI TE!", kW / 2, 13, 4, kWhite, MC_DATUM);
  } else {
    g->fillRect(0, 0, kW, 26, kHeader);
    text("IL VOLO PIU' VICINO", kW / 2, 13, 2, kCyan, MC_DATUM);
  }

  text(a.callsign[0] ? a.callsign : a.hex, 8, 32, 4, altColor(a));
  snprintf(buf, sizeof(buf), "%s  %s", a.type[0] ? a.type : "?", a.reg);
  text(buf, kW - 8, 38, 2, kWhite, TR_DATUM);

  // Compagnia e rotta (adsbdb.com)
  if (!f.route) {
    text("Cerco la rotta...", 8, 62, 2, kGrey);
  } else if (!f.route->known) {
    text("Rotta non disponibile", 8, 62, 2, kGrey);
  } else {
    fitCopy(buf, sizeof(buf), f.route->airline, 2, 300);
    text(buf, 8, 62, 2, kWhite);
    text(f.route->origCode, 8, 84, 4, kYellow);
    const int ax = 8 + g->textWidth(f.route->origCode, 4) + 8;
    g->drawFastHLine(ax, 96, 26, kYellow);
    g->fillTriangle(ax + 26, 92, ax + 26, 100, ax + 32, 96, kYellow);
    text(f.route->destCode, ax + 40, 84, 4, kYellow);
    fitCopy(buf, sizeof(buf), f.route->origCity, 1, 100);
    text(buf, 8, 112, 1, kGrey);
    fitCopy(buf, sizeof(buf), f.route->destCity, 1, 110);
    text(buf, ax + 40, 112, 1, kGrey);
  }

  // Dati di volo
  struct Row {
    const char* label;
    char value[28];
    bool deg;
  } rows[5] = {};
  rows[0].label = "Quota";
  if (a.hasAlt && !a.onGround) {
    snprintf(rows[0].value, sizeof(rows[0].value), "%ld m  (%ld ft)",
             lroundf(a.altFt * geo::kFtToM), static_cast<long>(a.altFt));
  } else {
    fmtAltitude(rows[0].value, sizeof(rows[0].value), a);
  }
  rows[1].label = "Velocita'";
  snprintf(rows[1].value, sizeof(rows[1].value), "%d km/h",
           static_cast<int>(lroundf(a.gsKt * geo::kKtToKmh)));
  rows[2].label = "Rotta";
  if (a.hasTrack) {
    snprintf(rows[2].value, sizeof(rows[2].value), "%d", static_cast<int>(lroundf(a.trackDeg)));
    rows[2].deg = true;
  } else {
    strlcpy(rows[2].value, "-", sizeof(rows[2].value));
  }
  rows[3].label = "Distanza";
  char d[12];
  fmtDistance(d, sizeof(d), a.distKm);
  snprintf(rows[3].value, sizeof(rows[3].value), "%s km a %s", d, geo::compass8(a.bearingDeg));
  rows[4].label = "Variometro";
  if (a.vrateFpm > 300) {
    snprintf(rows[4].value, sizeof(rows[4].value), "sale %d m/min",
             static_cast<int>(lroundf(a.vrateFpm * geo::kFtToM)));
  } else if (a.vrateFpm < -300) {
    snprintf(rows[4].value, sizeof(rows[4].value), "scende %d m/min",
             static_cast<int>(lroundf(-a.vrateFpm * geo::kFtToM)));
  } else {
    strlcpy(rows[4].value, "livellato", sizeof(rows[4].value));
  }

  for (int i = 0; i < 5; ++i) {
    const int y = 130 + i * 21;
    text(rows[i].label, 8, y + 4, 1, kGrey);
    if (rows[i].deg) {
      textDeg(rows[i].value, 76, y, 2, kWhite);
      const int w = g->textWidth(rows[i].value, 2);
      text(geo::compass8(a.trackDeg), 76 + w + 10, y, 2, kWhite);
    } else {
      text(rows[i].value, 76, y, 2, kWhite);
    }
  }
  drawVrateArrow(64, 130 + 4 * 21 + 8, a.vrateFpm);

  drawLookCompass(a, 266, 168, 34);
}

}  // namespace

// ---------------------------------------------------------------------------

bool begin() {
  tft.init();
  tft.setRotation(cfg::kRotation);
  tft.fillScreen(TFT_BLACK);
  setBacklight(true);

  spr.setColorDepth(8);
  s_useSprite = spr.createSprite(kW, kH) != nullptr;
  g = s_useSprite ? static_cast<TFT_eSPI*>(&spr) : &tft;
  Serial.printf("[ui] sprite %s, heap libero %u\n", s_useSprite ? "ok" : "NON allocato",
                ESP.getFreeHeap());
  return s_useSprite;
}

void setBacklight(bool on) {
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
#endif
}

void message(const char* title, const char* l1, const char* l2, const char* l3) {
  g->fillScreen(kBlack);
  g->fillRect(0, 0, kW, 26, kHeader);
  text("SOPRA IL CIELO", kW / 2, 13, 2, kCyan, MC_DATUM);
  text(title, kW / 2, 70, 4, kWhite, MC_DATUM);
  const char* lines[] = {l1, l2, l3};
  for (int i = 0; i < 3; ++i) {
    if (lines[i]) text(lines[i], kW / 2, 120 + i * 26, 2, i == 0 ? kYellow : kGrey, MC_DATUM);
  }
  if (s_useSprite) spr.pushSprite(0, 0);
}

void updateTrails(const Aircraft* ac, size_t count) {
  for (Trail& t : s_trails) {
    if (t.n && ++t.missed > 3) t.n = 0;  // sparito da 3 aggiornamenti: libera
  }
  for (size_t i = 0; i < count; ++i) {
    const Aircraft& a = ac[i];
    if (!a.hex[0]) continue;
    Trail* t = findTrail(a.hex);
    if (!t) {
      for (Trail& c : s_trails) {
        if (c.n == 0) {
          t = &c;
          break;
        }
      }
      if (!t) continue;
      strlcpy(t->hex, a.hex, sizeof(t->hex));
    }
    t->missed = 0;
    if (t->n > 0 && t->lat[t->n - 1] == a.lat && t->lon[t->n - 1] == a.lon) continue;
    if (t->n == cfg::kTrailPoints) {
      memmove(t->lat, t->lat + 1, sizeof(float) * (cfg::kTrailPoints - 1));
      memmove(t->lon, t->lon + 1, sizeof(float) * (cfg::kTrailPoints - 1));
      --t->n;
    }
    t->lat[t->n] = a.lat;
    t->lon[t->n] = a.lon;
    ++t->n;
  }
}

void render(View view, const Frame& f) {
  switch (view) {
    case View::Radar: renderRadar(f); break;
    case View::List: renderList(f); break;
    case View::Detail: renderDetail(f); break;
  }
  if (f.toast) drawToast(f.toast);
  if (s_useSprite) spr.pushSprite(0, 0);
}

}  // namespace ui
