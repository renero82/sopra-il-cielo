#pragma once

#include <stddef.h>

#include "model.h"

// Chiamate HTTP bloccanti: vanno usate SOLO dal task di rete.
namespace api {

// Posizione approssimata dall'IP pubblico (ip-api.com, poi ipwho.is).
// Precisione tipica: la citta' del tuo provider, quindi 1-30 km.
bool geolocateByIp(Location& out);

// Aerei entro radiusKm, ordinati dal piu' vicino. Fonti: adsb.lol, poi adsb.fi.
bool fetchAircraft(float lat, float lon, float radiusKm, Aircraft* out,
                   size_t maxCount, size_t& count, char* err, size_t errLen);

// Rotta del volo da adsbdb.com. true = risposta definitiva (anche "volo sconosciuto"),
// false = errore di rete, riprovare piu' tardi.
bool fetchRoute(const char* callsign, Route& out);

}  // namespace api
