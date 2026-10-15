#pragma once

#include <stddef.h>
#include <stdint.h>

// Strutture dati condivise tra il task di rete (core 0) e la UI (core 1).

struct Aircraft {
  char hex[7];        // indirizzo ICAO 24 bit, es. "4ca7b5"
  char callsign[9];   // es. "RYR12AB"
  char reg[11];       // marche, es. "EI-DCL"
  char type[5];       // codice ICAO tipo, es. "B738"
  float lat;
  float lon;
  float seenPosS;     // eta' della posizione al momento della richiesta
  int32_t altFt;      // quota barometrica
  float gsKt;         // velocita' al suolo
  float trackDeg;     // rotta al suolo
  int16_t vrateFpm;   // rateo verticale
  bool hasAlt;
  bool onGround;
  bool hasTrack;
  // Calcolati rispetto all'osservatore (aggiornati dal dead reckoning)
  float distKm;
  float bearingDeg;
};

struct Route {
  char callsign[9];
  bool known;         // false = adsbdb non conosce questo volo
  char airline[28];
  char origCode[5];   // IATA (o ICAO se manca)
  char origCity[22];
  char destCode[5];
  char destCity[22];
};

enum class LocationSource : uint8_t { None, Manual, Ip, Cached, Fallback };

struct Location {
  bool valid;
  float lat;
  float lon;
  char city[32];
  LocationSource source;
};
