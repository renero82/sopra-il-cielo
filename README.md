# Sopra il cielo

Un piccolo "Flightradar" da scrivania per ESP32: mostra in tempo reale gli aerei che ti passano sopra la testa, su un display da 2,8". Trova la tua posizione dall'indirizzo IP, oppure usa coordinate che inserisci tu.

```
┌──────────────────────────────┬─────────┐
│            N                 │SOPRA IL │
│      ·  ·  ·  ·  ·           │  21:42  │
│   ·    ▲ RYR57BR      ·      │ Milano  │
│  ·       \     ·  ·    ·     │ da IP   │
│ O   ·    ✈ AFR59FG     E     │Aerei 23 │
│  ·      (o)   ·       ·      │─────────│
│   ·  ITY1702 ◄       ·       │AFR59FG  │
│      ·  ·  ·  ·  ·           │BCS3     │
│            S                 │10676 m  │
└──────────────────────────────┴─────────┘
   radar con scie e sweep         pannello
```

## Cosa fa

- **Radar** con i cerchi delle distanze, lo sweep animato, le scie e un'icona per ogni aereo orientata secondo la sua rotta. Il colore indica la quota, come su FR24: arancio = basso, verde, azzurro, viola = alto.
- **Lista** dei voli più vicini: callsign, tipo, quota, velocità, distanza, direzione e una freccia se l'aereo sale o scende.
- **Dettaglio** del volo più vicino: compagnia, rotta (es. `TLV → MXP`), quota, velocità, variometro e una bussola che ti dice **dove guardare e quanto in alto** ("guarda a NE, 38° sull'orizzonte").
- **Avviso "SOPRA DI TE!"**: quando un aereo entra nel raggio impostato (3 km di default) il display passa da solo al dettaglio e il LED lampeggia di verde.
- **Movimento fluido**: i dati arrivano ogni 6 s, ma tra un aggiornamento e l'altro la posizione di ogni aereo viene stimata da rotta e velocità (dead reckoning), così le icone si muovono di continuo.
- **Configurazione dal browser**: portale Wi-Fi al primo avvio, poi raggiungibile su `http://sopra-il-cielo.local` per cambiare posizione e soglie.

## Hardware

| Componente | Note | Prezzo indicativo |
|---|---|---|
| **ESP32-2432S028R** ("Cheap Yellow Display", CYD) | ESP32 + display ILI9341 2,8" 320×240 + LED RGB, tutto integrato | 12–18 € |
| Cavo micro-USB (o USB-C sulla versione "2USB") | anche per alimentarlo | — |

Non serve saldare niente. Esistono due versioni della CYD: se i colori appaiono "in negativo", usa l'ambiente `cyd2usb` invece di `cyd`.

> Per usare un'altra scheda ESP32 con un display SPI basta cambiare i pin TFT in `platformio.ini` e i pin di LED e tasto in `include/config.h`.

## Installazione

1. Installa [VS Code](https://code.visualstudio.com/) con l'estensione **PlatformIO**, oppure la sola CLI: `pip install platformio`
2. Collega la CYD via USB
3. Compila e carica:

```bash
pio run -e cyd -t upload
```

4. (Facoltativo) Apri il monitor seriale per vedere cosa succede:

```bash
pio device monitor
```

## Primo avvio

1. Il display mostra **"Configura il Wi-Fi"**
2. Dal telefono, connettiti alla rete **`SopraIlCielo-Setup`** (di solito si apre da sola una pagina; altrimenti vai su `http://192.168.4.1`)
3. Scegli la tua rete Wi-Fi e inserisci la password
4. **Posizione**: lascia vuoti latitudine e longitudine per usare quella automatica dall'IP, oppure inserisci le tue coordinate (su Google Maps: tasto destro sul punto → clic sulle coordinate per copiarle)
5. Salva: la scheda si collega, trova la posizione e parte

Per tornare al portale in seguito: apri `http://sopra-il-cielo.local` (o l'IP mostrato all'avvio), oppure premi BOOT nei primi 2 secondi dopo l'accensione.

## Uso

| Tasto BOOT | Effetto |
|---|---|
| tocco breve | vista successiva: Radar → Lista → Dettaglio |
| tenuto 1–4 s | raggio successivo: 10 → 25 → 50 → 100 km |
| tenuto 6 s | reset completo (Wi-Fi e impostazioni) |

| LED | Significato |
|---|---|
| verde lampeggiante | un aereo è sopra di te |
| rosso a impulsi | problemi di rete o con le API |

## Come funziona

```
            core 0 (task "net")                         core 1 (loop)
 ┌──────────────────────────────────────┐   mutex   ┌───────────────────────────┐
 │ 1. posizione: manuale / ip-api.com / │ ────────► │ tasto BOOT                │
 │    ipwho.is / ultima nota            │  snapshot │ dead reckoning (25 fps)   │
 │ 2. ogni 6 s: aerei entro il raggio   │           │ scelta del volo "focus"   │
 │    adsb.lol  (ripiego: adsb.fi)      │           │ disegno su sprite 8 bit   │
 │ 3. rotte dei voli più vicini         │           │ LED                       │
 │    adsbdb.com (con cache)            │           │ portale web (WiFiManager) │
 └──────────────────────────────────────┘           └───────────────────────────┘
```

- **Rete e UI su due core**: le richieste HTTPS (1–2 s con l'handshake TLS) non bloccano mai l'animazione.
- **Parsing in streaming con filtro**: le risposte ADS-B superano i 50 KB nelle zone trafficate. ArduinoJson le legge direttamente dal socket, tiene solo i 14 campi che servono e scarta il resto. Esempio reale su Milano: 25 KB di JSON diventano 8 KB in memoria.
- **Solo i 40 aerei più vicini** vengono tenuti, ordinati per distanza.
- **Uno sprite a 8 bit da 320×240 (75 KB)** viene disegnato e inviato al display in un colpo solo, quindi niente sfarfallio. Viene allocato all'avvio, prima del Wi-Fi, finché la RAM è ancora contigua.

### Struttura

```
include/config.h     tutte le costanti (pin, intervalli, raggi, fuso orario)
include/geo.h        geometria: distanza, rilevamento, dead reckoning, elevazione
include/parse.h      parsing JSON di adsb.lol / adsb.fi / adsbdb
src/api.cpp          richieste HTTP(S)
src/tracker.cpp      task di rete e stato condiviso
src/ui.cpp           le tre viste
src/settings.cpp     impostazioni persistenti (NVS)
src/main.cpp         setup, Wi-Fi, tasto, LED, loop
test/host_test.cpp   test sul PC con dati reali
```

## Fonti dati: perché non Flightradar24

FR24 non ha un'API gratuita: quella ufficiale è a pagamento, e usare il feed interno del sito ne viola i termini d'uso. Sopra il cielo usa invece reti aperte di ricevitori ADS-B gestite da volontari, che mostrano gli stessi aerei, compresi molti che FR24 filtra:

| Servizio | Uso | Condizioni |
|---|---|---|
| [adsb.lol](https://adsb.lol) | posizioni (fonte principale) | dati ODbL, nessuna chiave |
| [adsb.fi](https://adsb.fi) | posizioni (ripiego) | solo uso personale e non commerciale, max 1 richiesta/s |
| [adsbdb.com](https://www.adsbdb.com) | compagnia e rotta dal callsign | gratuito |
| [ip-api.com](https://ip-api.com) / [ipwho.is](https://ipwho.is) | posizione da IP | gratuito per uso non commerciale |

Le API gratuite possono cambiare: se adsb.lol smette di rispondere, il firmware passa da solo ad adsb.fi. Le fonti si cambiano in `src/api.cpp`.

## Limiti da conoscere

- **La posizione da IP è approssimata**: di solito indica la città del tuo provider, con errori da 1 a 30 km. Per un avviso "sopra di te" affidabile inserisci le coordinate nel portale.
- **Ritardo**: i dati aggregati arrivano con qualche secondo di ritardo. Il dead reckoning lo compensa in parte.
- **Certificati TLS non verificati** (`setInsecure()`): i dati sono pubblici e in sola lettura. È una scelta voluta, per non dover aggiornare il firmware quando scadono le CA.
- **Touch non usato**: la CYD ha un touch XPT2046, ma per ora si usa solo il tasto BOOT.

## Test sul PC

La logica che non dipende dall'hardware (geometria e parsing) si testa sul computer, anche con risposte reali delle API:

```bash
pio run -e cyd
```

```bash
c++ -std=c++17 -Iinclude -I.pio/libdeps/cyd/ArduinoJson/src test/host_test.cpp -o host_test
```

```bash
curl -s "https://api.adsb.lol/v2/lat/45.4642/lon/9.19/dist/54" -o adsb.json && ./host_test adsb.json
```

(Il primo comando serve solo a scaricare ArduinoJson in `.pio/libdeps`.)

## Idee per il futuro

- **Ricevitore locale**: una chiavetta RTL-SDR con dump1090/readsb su un Raspberry Pi dà dati davvero in tempo reale e indipendenti da internet. Basta puntare `kAdsbSources` a `http://<raspberry>/data/aircraft.json`, che usa lo stesso formato.
- Touch per selezionare un aereo sul radar
- Buzzer per l'avviso "sopra di te"
- Aggiornamenti OTA
- Case stampato in 3D

## Crediti e licenza

MIT, vedi [LICENSE](LICENSE).

L'approccio (WiFiManager + posizione in NVS, filtro ArduinoJson sulle risposte readsb) si ispira a [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) (MIT), che è anche un'ottima alternativa per un display rotondo GC9A01 con ESP32-C3.

Librerie: [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (FreeBSD), [ArduinoJson](https://arduinojson.org) (MIT), [WiFiManager](https://github.com/tzapu/WiFiManager) (MIT).
