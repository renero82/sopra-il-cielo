#pragma once

#include <stddef.h>
#include <stdint.h>

#include "model.h"
#include "tracker.h"

namespace ui {

enum class View : uint8_t { Radar, List, Detail };

// Tutto quello che serve per disegnare un fotogramma
struct Frame {
  const Aircraft* ac;  // posizioni "live" (estrapolate), ordinate per distanza
  size_t count;
  int focus;           // indice dell'aereo in evidenza, -1 = nessuno
  bool overhead;       // l'aereo in evidenza e' sopra di te
  const Route* route;  // rotta dell'aereo in evidenza, nullptr = non ancora nota
  const tracker::Status* status;
  float rangeKm;
  float overheadKm;
  uint32_t nowMs;
  const char* toast;   // messaggio temporaneo in sovrimpressione, nullptr = nessuno
};

bool begin();
void setBacklight(bool on);
void message(const char* title, const char* l1 = nullptr, const char* l2 = nullptr,
             const char* l3 = nullptr);
void updateTrails(const Aircraft* ac, size_t count);
void render(View view, const Frame& f);

}  // namespace ui
