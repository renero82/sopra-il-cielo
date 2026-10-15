#pragma once

#include <stddef.h>
#include <stdint.h>

// Tutte le impostazioni "da compilazione" in un unico posto.
// Quelle modificabili a runtime (posizione manuale, soglia "sopra di te")
// si impostano dal portale web e finiscono in NVS (vedi settings.cpp).
namespace cfg {

// --- Wi-Fi / portale -------------------------------------------------------
constexpr char kApName[] = "SopraIlCielo-Setup";
constexpr char kHostname[] = "sopra-il-cielo";  // http://sopra-il-cielo.local
constexpr uint16_t kPortalTimeoutS = 300;

// --- Ora locale (POSIX TZ). Italia: CET/CEST --------------------------------
constexpr char kTimezone[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char kNtpServer[] = "pool.ntp.org";

// --- Posizione di ripiego se IP-geolocation e cache falliscono (Roma) -------
constexpr float kFallbackLat = 41.9028f;
constexpr float kFallbackLon = 12.4964f;

// --- Raggio del radar (km). Pressione media del tasto = preset successivo ---
constexpr uint16_t kRangesKm[] = {10, 25, 50, 100};
constexpr uint8_t kRangeCount = sizeof(kRangesKm) / sizeof(kRangesKm[0]);
constexpr uint8_t kDefaultRangeIdx = 1;

// Entro questa distanza orizzontale un aereo e' "sopra di te"
constexpr float kDefaultOverheadKm = 3.0f;
// Passa da solo alla vista dettaglio quando un aereo e' sopra di te
constexpr bool kAutoDetailOnOverhead = true;

// --- Rete ------------------------------------------------------------------
// adsb.lol: dati ODbL, nessuna chiave. Non scendere sotto ~5 s per cortesia.
constexpr uint32_t kPollIntervalMs = 6000;
constexpr uint32_t kHttpTimeoutMs = 10000;
constexpr float kQueryMarginFactor = 1.15f;  // chiedi un po' oltre il bordo
constexpr float kMaxSeenPosS = 60.0f;        // scarta posizioni piu' vecchie
constexpr float kMaxExtrapolationS = 30.0f;  // dead-reckoning massimo
constexpr size_t kMaxAircraft = 40;          // i piu' vicini
constexpr uint8_t kRouteLookupsPerPoll = 2;  // richieste adsbdb per ciclo
constexpr size_t kRouteCacheSize = 24;

// --- Hardware CYD -----------------------------------------------------------
constexpr int kButtonPin = 0;  // BOOT, attivo basso
constexpr int kLedR = 4;       // LED RGB, attivo basso. -1 per disabilitare
constexpr int kLedG = 16;
constexpr int kLedB = 17;
constexpr bool kLedActiveLow = true;
constexpr uint8_t kRotation = 1;  // 1 = orizzontale, USB a destra; 3 = ruotato 180

// --- Tasto ------------------------------------------------------------------
constexpr uint32_t kShortPressMaxMs = 700;   // < cambia vista
constexpr uint32_t kRangePressMaxMs = 4000;  // < cambia raggio
constexpr uint32_t kFactoryResetMs = 6000;   // tieni premuto: reset totale

// --- UI ---------------------------------------------------------------------
constexpr uint32_t kFrameIntervalMs = 40;  // ~25 fps
constexpr uint32_t kSweepPeriodMs = 4000;
constexpr uint8_t kTrailPoints = 8;  // scia: ultime N posizioni ricevute
constexpr uint8_t kMaxLabels = 8;    // etichette sul radar (i piu' vicini)

}  // namespace cfg
