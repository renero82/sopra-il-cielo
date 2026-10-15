#pragma once

#include <stddef.h>
#include <stdint.h>

#include "model.h"

// Task di rete (core 0): geolocalizzazione, polling ADS-B, rotte dei voli.
// La UI legge da qui copie protette da mutex, senza mai bloccarsi sulla rete.
namespace tracker {

struct Status {
  Location loc;
  bool netOk;
  char netErr[48];
  uint32_t lastFetchMs;  // millis() dell'ultimo aggiornamento riuscito, 0 = mai
  uint8_t rangeIdx;
};

void begin(bool manualLocation, float lat, float lon, uint8_t rangeIdx);

// Copia gli aerei se ci sono dati piu' recenti di `version`. Ritorna true se ha copiato.
bool copyIfNewer(uint32_t& version, Aircraft* out, size_t& count, uint32_t& fetchedMs);
void getStatus(Status& out);
bool lookupRoute(const char* callsign, Route& out);

// Comandi dalla UI / dal portale web (svegliano subito il task di rete)
void setRangeIdx(uint8_t idx);
void setLocation(bool manual, float lat, float lon);

}  // namespace tracker
