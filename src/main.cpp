// Sopra il cielo — traffico aereo in tempo reale sopra la tua testa, su ESP32.
//
// Core 0: task di rete (tracker.cpp)  -> IP geolocation, adsb.lol, adsbdb
// Core 1: questo loop                 -> tasto, dead reckoning, disegno a ~25 fps

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include <vector>

#include "config.h"
#include "geo.h"
#include "settings.h"
#include "tracker.h"
#include "ui.h"

namespace {

WiFiManager wm;
settings::Settings s_cfg;
bool s_trackerStarted = false;

// Campi extra del portale di configurazione
char s_latBuf[16] = "";
char s_lonBuf[16] = "";
char s_ovhBuf[8] = "";
WiFiManagerParameter p_help(
    "<p><b>Posizione</b>: lascia vuoti latitudine e longitudine per usare quella "
    "approssimata del tuo IP. Per una precisione al metro inserisci le tue coordinate "
    "(su Google Maps: tasto destro sul punto &rarr; copia le coordinate).</p>");
WiFiManagerParameter p_lat("lat", "Latitudine (es. 43.7696)", s_latBuf, 15);
WiFiManagerParameter p_lon("lon", "Longitudine (es. 11.2558)", s_lonBuf, 15);
WiFiManagerParameter p_ovh("ovh", "Raggio 'sopra di te' in km", s_ovhBuf, 7);

// Stato della UI
ui::View s_view = ui::View::Radar;
ui::View s_viewBeforeAuto = ui::View::Radar;
bool s_autoDetail = false;
char s_autoHex[7] = "";
char s_toast[32] = "";
uint32_t s_toastUntil = 0;

Aircraft s_base[cfg::kMaxAircraft];  // ultimo snapshot dalla rete
Aircraft s_live[cfg::kMaxAircraft];  // posizioni estrapolate a "adesso"
size_t s_baseCount = 0;
uint32_t s_baseMs = 0;
uint32_t s_version = 0;

void showToast(const char* msg, uint32_t ms = 1500) {
  strlcpy(s_toast, msg, sizeof(s_toast));
  s_toastUntil = millis() + ms;
}

bool parseCoord(const char* s, float& out) {
  char buf[16];
  strlcpy(buf, s, sizeof(buf));
  for (char* c = buf; *c; ++c) {
    if (*c == ',') *c = '.';  // accetta anche la virgola decimale
  }
  char* end = nullptr;
  out = strtof(buf, &end);
  return end != buf;
}

void fillPortalFields() {
  if (s_cfg.manualLocation) {
    snprintf(s_latBuf, sizeof(s_latBuf), "%.5f", s_cfg.lat);
    snprintf(s_lonBuf, sizeof(s_lonBuf), "%.5f", s_cfg.lon);
  } else {
    s_latBuf[0] = s_lonBuf[0] = '\0';
  }
  snprintf(s_ovhBuf, sizeof(s_ovhBuf), "%.1f", s_cfg.overheadKm);
  p_lat.setValue(s_latBuf, 15);
  p_lon.setValue(s_lonBuf, 15);
  p_ovh.setValue(s_ovhBuf, 7);
}

// Chiamata dal portale quando l'utente salva (sia al primo setup sia da http://sopra-il-cielo.local)
void onSaveParams() {
  float lat = NAN, lon = NAN, ovh = 0;
  const bool hasLat = parseCoord(p_lat.getValue(), lat);
  const bool hasLon = parseCoord(p_lon.getValue(), lon);
  const bool manual = hasLat && hasLon && geo::validLatLon(lat, lon);
  settings::saveManualLocation(manual, lat, lon);
  if (parseCoord(p_ovh.getValue(), ovh) && ovh > 0.1f && ovh < 50.0f) {
    settings::saveOverheadKm(ovh);
  }
  settings::load(s_cfg);
  Serial.printf("[cfg] posizione %s %.5f %.5f, sopra di te %.1f km\n",
                s_cfg.manualLocation ? "manuale" : "da IP", s_cfg.lat, s_cfg.lon, s_cfg.overheadKm);
  if (s_trackerStarted) {
    tracker::setLocation(s_cfg.manualLocation, s_cfg.lat, s_cfg.lon);
    showToast("Impostazioni salvate");
  }
}

void onPortalStarted(WiFiManager*) {
  char ap[48];
  snprintf(ap, sizeof(ap), "1) Connettiti alla rete: %s", cfg::kApName);
  ui::message("Configura il Wi-Fi", ap, "2) Apri http://192.168.4.1", "3) Scegli la tua rete e salva");
}

void setLed(bool r, bool gr, bool b) {
  const int pins[] = {cfg::kLedR, cfg::kLedG, cfg::kLedB};
  const bool on[] = {r, gr, b};
  for (int i = 0; i < 3; ++i) {
    if (pins[i] >= 0) digitalWrite(pins[i], on[i] != cfg::kLedActiveLow);
  }
}

void factoryReset() {
  ui::message("Reset completo", "Cancello Wi-Fi e impostazioni...");
  setLed(true, false, false);
  wm.resetSettings();
  settings::clearAll();
  delay(1500);
  ESP.restart();
}

void nextView() {
  s_autoDetail = false;  // l'utente ha scelto: non tornare indietro da solo
  switch (s_view) {
    case ui::View::Radar: s_view = ui::View::List; break;
    case ui::View::List: s_view = ui::View::Detail; break;
    case ui::View::Detail: s_view = ui::View::Radar; break;
  }
}

void nextRange() {
  s_cfg.rangeIdx = (s_cfg.rangeIdx + 1) % cfg::kRangeCount;
  settings::saveRangeIdx(s_cfg.rangeIdx);
  tracker::setRangeIdx(s_cfg.rangeIdx);
  char msg[24];
  snprintf(msg, sizeof(msg), "Raggio %u km", cfg::kRangesKm[s_cfg.rangeIdx]);
  showToast(msg);
}

// Tocco breve: vista successiva. ~1-4 s: raggio successivo. 6 s: reset totale.
void handleButton(uint32_t now) {
  static bool wasDown = false;
  static uint32_t downSince = 0;
  const bool down = digitalRead(cfg::kButtonPin) == LOW;

  if (down && !wasDown) downSince = now;
  if (down) {
    const uint32_t held = now - downSince;
    if (held >= cfg::kFactoryResetMs) factoryReset();
    if (held >= cfg::kRangePressMaxMs) {
      showToast("Tieni premuto: RESET", 300);
    } else if (held >= cfg::kShortPressMaxMs) {
      showToast("Rilascia: raggio", 300);
    }
  } else if (wasDown) {
    const uint32_t held = now - downSince;
    if (held >= 30 && held < cfg::kShortPressMaxMs) {
      nextView();
    } else if (held >= cfg::kShortPressMaxMs && held < cfg::kRangePressMaxMs) {
      nextRange();
    }
  }
  wasDown = down;
}

// Porta ogni aereo alla posizione stimata "adesso" (rotta x velocita' x tempo trascorso)
size_t extrapolate(const Location& loc, uint32_t now) {
  const float elapsedS = (now - s_baseMs) / 1000.0f;
  for (size_t i = 0; i < s_baseCount; ++i) {
    Aircraft a = s_base[i];
    if (!a.onGround && a.hasTrack && a.gsKt > 30.0f) {
      const float dt = min(elapsedS + a.seenPosS, cfg::kMaxExtrapolationS);
      geo::moveAlong(a.lat, a.lon, a.trackDeg, a.gsKt * geo::kKtToKmh * dt / 3600.0f);
    }
    const geo::Vec v = geo::toLocal(loc.lat, loc.lon, a.lat, a.lon);
    a.distKm = geo::lengthKm(v);
    a.bearingDeg = geo::bearingDeg(v);

    size_t j = i;  // insertion sort per distanza (pochi elementi, quasi ordinati)
    while (j > 0 && s_live[j - 1].distKm > a.distKm) {
      s_live[j] = s_live[j - 1];
      --j;
    }
    s_live[j] = a;
  }
  return s_baseCount;
}

int pickFocus(size_t count, float rangeKm) {
  for (size_t i = 0; i < count; ++i) {
    if (!s_live[i].onGround && s_live[i].distKm <= rangeKm) return static_cast<int>(i);
  }
  return -1;
}

// Quando un aereo entra nel cerchio "sopra di te" mostra il dettaglio, poi torna alla vista di prima
void autoSwitch(int focus, bool overhead) {
  if (!cfg::kAutoDetailOnOverhead) return;
  if (overhead) {
    const char* hex = s_live[focus].hex;
    if (strcmp(s_autoHex, hex) != 0) {
      strlcpy(s_autoHex, hex, sizeof(s_autoHex));
      if (s_view != ui::View::Detail) {
        s_viewBeforeAuto = s_view;
        s_view = ui::View::Detail;
        s_autoDetail = true;
      }
    }
  } else {
    s_autoHex[0] = '\0';
    if (s_autoDetail) {
      s_view = s_viewBeforeAuto;
      s_autoDetail = false;
    }
  }
}

void updateLed(bool overhead, bool netOk, uint32_t now) {
  if (overhead) {
    setLed(false, (now / 250) % 2, false);  // verde lampeggiante: guarda su!
  } else if (!netOk) {
    setLed((now / 1000) % 4 == 0, false, false);  // rosso a impulsi: problemi di rete
  } else {
    setLed(false, false, false);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("\n[Sopra il cielo] avvio");

  pinMode(cfg::kButtonPin, INPUT_PULLUP);
  for (int pin : {cfg::kLedR, cfg::kLedG, cfg::kLedB}) {
    if (pin >= 0) pinMode(pin, OUTPUT);
  }
  setLed(false, false, false);

  // Lo sprite va allocato prima del Wi-Fi, finche' la RAM e' ancora contigua
  ui::begin();

  settings::load(s_cfg);
  fillPortalFields();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg::kHostname);
  wm.setHostname(cfg::kHostname);
  wm.setTitle("Sopra il cielo");
  wm.addParameter(&p_help);
  wm.addParameter(&p_lat);
  wm.addParameter(&p_lon);
  wm.addParameter(&p_ovh);
  wm.setSaveParamsCallback(onSaveParams);
  wm.setSaveConfigCallback(onSaveParams);
  wm.setAPCallback(onPortalStarted);
  wm.setConfigPortalTimeout(cfg::kPortalTimeoutS);
  std::vector<const char*> menu = {"wifi", "param", "info", "restart"};
  wm.setMenu(menu);

  // BOOT (GPIO0) premuto gia' all'accensione manda l'ESP32 in modalita' download:
  // per riaprire il portale si preme invece nei primi 2 secondi dopo l'avvio.
  ui::message("Avvio...", "Premi BOOT ora per configurare", "il Wi-Fi e la posizione");
  bool forcePortal = false;
  for (uint32_t t0 = millis(); millis() - t0 < 2000 && !forcePortal;) {
    forcePortal = digitalRead(cfg::kButtonPin) == LOW;
    delay(20);
  }
  if (!forcePortal) ui::message("Avvio...", "Connessione al Wi-Fi");
  const bool connected = forcePortal ? wm.startConfigPortal(cfg::kApName) : wm.autoConnect(cfg::kApName);
  if (!connected) {
    ui::message("Wi-Fi non configurato", "Riavvio tra 3 secondi...");
    delay(3000);
    ESP.restart();
  }
  Serial.printf("[wifi] connesso, IP %s\n", WiFi.localIP().toString().c_str());

  settings::load(s_cfg);
  configTzTime(cfg::kTimezone, cfg::kNtpServer);

  // Il portale resta raggiungibile in rete locale per cambiare posizione e soglie
  wm.setConfigPortalBlocking(false);
  wm.startWebPortal();

  char ip[40];
  snprintf(ip, sizeof(ip), "Impostazioni: http://%s", WiFi.localIP().toString().c_str());
  ui::message("Connesso!", ip, "Cerco la tua posizione...");

  tracker::begin(s_cfg.manualLocation, s_cfg.lat, s_cfg.lon, s_cfg.rangeIdx);
  s_trackerStarted = true;
  delay(1500);
}

void loop() {
  wm.process();

  const uint32_t now = millis();
  handleButton(now);

  static uint32_t lastFrame = 0;
  if (now - lastFrame < cfg::kFrameIntervalMs) {
    delay(2);
    return;
  }
  lastFrame = now;

  if (tracker::copyIfNewer(s_version, s_base, s_baseCount, s_baseMs)) {
    ui::updateTrails(s_base, s_baseCount);
  }

  tracker::Status st;
  tracker::getStatus(st);
  const float rangeKm = cfg::kRangesKm[s_cfg.rangeIdx];

  const size_t count = st.loc.valid ? extrapolate(st.loc, now) : 0;
  const int focus = pickFocus(count, rangeKm);
  const bool overhead = focus >= 0 && s_live[focus].distKm <= s_cfg.overheadKm;
  autoSwitch(focus, overhead);

  Route route = {};
  bool hasRoute = false;
  if (focus >= 0) {
    // Senza callsign non c'e' rotta da cercare: mostra subito "non disponibile"
    hasRoute = !s_live[focus].callsign[0] || tracker::lookupRoute(s_live[focus].callsign, route);
  }

  ui::Frame f = {};
  f.ac = s_live;
  f.count = count;
  f.focus = focus;
  f.overhead = overhead;
  f.route = hasRoute ? &route : nullptr;
  f.status = &st;
  f.rangeKm = rangeKm;
  f.overheadKm = s_cfg.overheadKm;
  f.nowMs = now;
  f.toast = static_cast<int32_t>(s_toastUntil - now) > 0 ? s_toast : nullptr;
  ui::render(s_view, f);

  updateLed(overhead, st.netOk, now);
}
