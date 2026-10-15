#pragma once

#include "model.h"

// Impostazioni persistenti in NVS (sopravvivono a riavvii e aggiornamenti firmware).
namespace settings {

struct Settings {
  bool manualLocation;  // true = usa lat/lon sotto invece dell'IP
  float lat;
  float lon;
  uint8_t rangeIdx;
  float overheadKm;
};

void load(Settings& out);
void saveManualLocation(bool manual, float lat, float lon);
void saveRangeIdx(uint8_t idx);
void saveOverheadKm(float km);

// Ultima posizione ottenuta via IP: usata se al prossimo avvio il servizio non risponde
void saveCachedLocation(const Location& loc);
bool loadCachedLocation(Location& out);

void clearAll();

}  // namespace settings
