# Speicheranalyse HomeTiles-Firmware: interner RAM und Refactoring des eigenen Codes

Stand: 2026-09-27, Firmware-Quellen `b496bc6` (identisch mit `c3e0a67`, dem Stand
des CI-Laufs `36334088502`; zwischen beiden Commits ändern sich nur Doku und
`PROJECT_CONTEXT.md`). Reine Analyse, am Code ist nichts geändert.

Schwerpunkte:

1. **Interner RAM** (`MALLOC_CAP_INTERNAL`/`DMA`), getrennt nach ESP32-P4 und ESP32-S3.
2. **Refactoring des eigenen Codes** (Flash): doppelte Logik zwischen Kacheltypen
   und Popups, toter bzw. selten genutzter Code, Debug-/Diagnose-Code und Log-Texte,
   Bibliotheken mit schlechtem Kosten/Nutzen-Verhältnis, Aufblähung durch Templates,
   Inline-Funktionen und Sprachfeatures.
3. Assets (Fonts, Web-Assets, i18n) nur als Kontext in Abschnitt 6.

Kennzeichnung: **gemessen** = aus ELF/Map/CI-Log abgelesen oder per Messbuild
ermittelt; **Schätzung** = begründete Abschätzung, Begründung steht dabei.
1 KiB = 1024 Byte.

---

## 1. Kurzfassung

Die zehn Maßnahmen mit dem besten Verhältnis von Nutzen zu Aufwand (vollständige,
sortierte Liste in Abschnitt 7):

| # | Maßnahme | Einsparung | Speicher | Aufwand | Risiko | Geräte |
|---|---|---|---|---|---|---|
| 1 | C++ ohne Exceptions bauen (`-fno-exceptions`; kein `try/catch` im Code) | 295 KiB P4-8" / 310 KiB S3 (gemessen) | Flash | S | niedrig–mittel | alle |
| 2 | Notfallpuffer `g_climate_emergency_states` entfernen/verkleinern | 12,9 KiB P4-8" / 5,9 KiB S3 (gemessen) | intern DRAM (+ gleich viel Flash, liegt in `.data`) | S | niedrig | alle |
| 3 | S3: Wetter-/Media-Widgets und Entity-Cache wie auf P4 in PSRAM | 23,6 KiB (gemessen) | intern DRAM | S | niedrig–mittel | S3 |
| 4 | Binary-Sensor-Zustände/-Widgets in PSRAM | 13,7 KiB P4-8" / 6,3 KiB S3 (gemessen) | intern DRAM | S | niedrig | alle |
| 5 | Ungenutzte LVGL-Widgets und Farbformate abschalten (`lv_conf.h`) | 51,0 KiB Flash, davon 21,1 KiB IRAM (P4-8", gemessen) | Flash + IRAM | S | niedrig | alle |
| 6 | Übrige Kachel-Arrays (Sensor/Switch/Cover/Climate) in einen PSRAM-Block | 26,4 KiB P4-8" / 12,1 KiB S3 (gemessen) | intern DRAM | M | niedrig–mittel | alle |
| 7 | `TileGridConfig` von `tileConfig`/`screensaverConfig` in PSRAM | 14,2 KiB P4-8" / 6,7 KiB S3 (gemessen) | intern DRAM | M | niedrig | alle |
| 8 | Eigener Log-Helfer mit festem Puffer statt `Serial.printf` (kein `malloc` je Zeile ≥ 64 Zeichen), Debug-Stufe zur Compile-Zeit | 20–35 KiB Flash (Schätzung), plus weniger Heap-Wechsel im internen RAM | Flash + interner Heap | M | niedrig–mittel | alle |
| 9 | ArduinoJson mit PSRAM-Allocator (Pools à 1 KiB landen sonst intern) | bis ~30 KiB interne Spitzenlast je großem Dokument (Schätzung) | interner Heap (transient) | S | niedrig | alle |
| 10 | JSON-Behandlung bündeln (ArduinoJson-Klone in 6+ Übersetzungseinheiten, drei handgeschriebene Scanner) | 10–15 KiB (Schätzung, Basis gemessen) | Flash | M | niedrig–mittel | alle |

Die wichtigsten Einsichten:

- **Die Firmware selbst belegt den größten Teil des statischen internen DRAM.**
  Auf dem 8"-P4 stammen 93,7 KiB der 132,5 KiB statischen DRAM aus eigenem Code, auf
  dem S3 77,8 KiB von 136,3 KiB (gemessen). Fast alles davon sind Arrays pro
  Kachelslot und Grid, die für jeden Slot angelegt werden, auch wenn dort keine
  Kachel dieses Typs liegt. PSRAM-Varianten existieren bereits für einen Teil
  (P4 „cold storage“, Ordner-Cache) – sie sind nur nicht konsequent umgesetzt.
- **`EXT_RAM_BSS_ATTR` hilft hier nicht:** In den vorkompilierten Arduino-3.3.7-
  Bibliotheken ist `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` aus; das Makro
  expandiert zu nichts. Statische Arrays müssen auf `heap_caps_*(MALLOC_CAP_SPIRAM)`
  umgestellt werden.
- **Die 4-KiB-Regel entscheidet über den dynamischen internen RAM.**
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`: Jedes `malloc`/`new` bis 4096 Byte geht
  intern. Das betrifft Arduino-`String` (jede Zeichenkette über 15 Zeichen), die
  1-KiB-Pools von ArduinoJson, jede `Serial.printf`-Zeile ab 64 Zeichen und
  mittelgroße Bridge-Listen. Das ist die plausibelste Ursache für die Fragmentierung
  nach langer Laufzeit.
- **LVGL legt 50–53 KiB Zeichencode ins IRAM** (`LV_ATTRIBUTE_FAST_MEM IRAM_ATTR`,
  `lv_conf.h:606`), davon Blend-Routinen für Farbformate, die HomeTiles nicht nutzt.
- **Flash:** Knapp wird es nicht auf den S3-Boards (80 % des OTA-Slots), sondern auf
  den 1280×800-P4-Boards und vor allem dem Tab5 (98,3 % des 6,5-MiB-OTA-Slots, nur
  110 KiB Reserve). Beim eigenen Code sind die größten Hebel die Exceptions (Messbuild: −295 KiB
  P4, −310 KiB S3, ein Build-Flag), Arduino-`String` (49 % aller Aufrufe im eigenen
  Code) und das serverseitig zusammengesetzte Web-HTML (~150 KiB).

---

## 2. Datenbasis und Methodik

**CI-Größen (gemessen):** Aus den Job-Logs von CI-Lauf `36334088502` (Commit `c3e0a67`)
die `arduino-cli`-Zeilen „Sketch uses“ (App-Image) und „Global variables“
(`.dram0.data` + `.dram0.bss` + `.dram1.*` + `.noinit`) für alle 17 Profile.

**Lokale Reproduktion (gemessen):** `arduino-cli` 1.3.1, `esp32:esp32@3.3.7`, dieselben
Bibliotheksversionen wie `sketch.yaml`, dieselben Build-Flags wie
`.github/workflows/firmware.yml` und dieselben ESP-Hosted-Objekte (`repo-a8204`) wie
die CI. Gebaut wurden Waveshare 8" (`waveshare_8`, ESP32-P4 vor v3), GUITION
ESP32-4848S040 (`guition_esp32_4848s040`, ESP32-S3) und M5Stack Tab5 (`tab5`).

| Profil | Global variables lokal / CI | Sketch lokal / CI | Abweichung |
|---|---|---|---|
| waveshare_8 | 135.632 / 135.632 | 6.586.430 / 6.586.102 | +328 B (Pfad-Strings) |
| guition_esp32_4848s040 | 139.576 / 139.576 | 5.474.078 / 5.473.726 | +352 B (Pfad-Strings) |
| tab5 | 124.168 / 124.352 | 6.688.340 / 6.703.082 | −14,4 KiB (Ursache offen) |

Die P4- und S3-Builds entsprechen der CI; beim Tab5 werden nur relative Anteile
verwendet, Flash-Reserven kommen aus der CI.

**Auswertung:**

- `size -A` für Sektionen, `nm -S -l -C` für jedes Symbol mit Größe und
  Quelldatei:Zeile (aus DWARF), Map-Datei für die Zuordnung zu Objektdateien und
  Bibliotheken, `objdump -d` für Aufrufstellen und für die Suche nach
  instruktionsgleichen Funktionen (5-Gramm-Shingles, Jaccard ≥ 0,5).
- Zwei **Messbuilds** in einer Scratch-Kopie (nicht im Repo): (a) eigener C++-Code
  mit `-fno-exceptions`, (b) `lv_conf.h` mit abgeschalteten, nicht genutzten Widgets
  und Farbformaten.
- Zeilennummern beziehen sich auf `b496bc6`.

**Grenzen:** Laufzeit-Heap (freier interner Speicher, größter Block, Task-Stack-
Hochwassermarken) lässt sich ohne Hardware nicht messen; diese Werte sind als
Schätzung gekennzeichnet. Die Map weist zusammengeführte String-Literale dem ersten
Objekt zu. Deshalb sind String-Anteile pro Datei Obergrenzen vor dem Zusammenführen;
Symbolgrößen aus `nm` sind exakt.

---

## 3. Wo interner RAM gebunden wird

### 3.1 Statischer Fußabdruck (gemessen)

| | P4 (Waveshare 8") | S3 (4848S040) |
|---|---|---|
| IRAM-Code (`.iram0.text` + Vektoren) | 131.968 B | 136.960 B |
| davon LVGL (`LV_ATTRIBUTE_FAST_MEM`) | 51.368 B | 53.761 B |
| statisches DRAM (`.data` + `.bss`) | 135.632 B | 139.576 B |
| davon eigener Code (Symbolsumme) | 95.992 B | 79.654 B |
| Summe statisch intern | ~261 KiB | ~270 KiB |

Zum Vergleich (Schätzung, ohne ROM-Reservierungen): Der P4 hat 768 KiB L2MEM, davon
128 KiB als L2-Cache konfiguriert (`CONFIG_CACHE_L2_CACHE_128KB`). Der S3 hat 512 KiB
SRAM, von dem Instruktions-/Datencache und ROM-Bereiche abgehen. IRAM-Code verkleinert
auf beiden Chips direkt den internen Heap.

Rahmenbedingungen aus der vorkompilierten `sdkconfig` von Arduino 3.3.7 (nicht per
Firmware-Code änderbar, aber für jede Maßnahme entscheidend):

| Einstellung | P4 | S3 | Folge |
|---|---|---|---|
| `SPIRAM_MALLOC_ALWAYSINTERNAL` | 4096 | 4096 | `malloc` ≤ 4 KiB immer intern |
| `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` | aus | aus | `EXT_RAM_BSS_ATTR` wirkungslos |
| `SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | – | an | S3: WLAN/lwIP-Puffer bevorzugt PSRAM |
| `MBEDTLS_INTERNAL_MEM_ALLOC`, `SSL_MAX_CONTENT_LEN` | an, 16384 | an, 16384 | TLS-Sitzung intern, ~40–50 KiB (Schätzung) |
| `LWIP_TCP_WND_DEFAULT` / `SND_BUF` | 65534 / 65534 | 5760 / 5744 | P4: bis 64 KiB unbestätigte Daten je Socket |
| `HEAP_POISONING_LIGHT` | an | an | Mehraufwand je Allokation |
| `COMPILER_CXX_EXCEPTIONS` | an | an | Unwind-Tabellen für jeden C++-Code |

### 3.2 Statische Daten des eigenen Codes (gemessen, `nm`)

Größen in Byte, P4 = Waveshare 8" (35 Slots pro Grid), S3 = 4848S040 (16 Slots).
Alle Einträge liegen heute im internen DRAM.

| Symbol | Datei:Zeile | P4 | S3 | PSRAM möglich? |
|---|---|---|---|---|
| `g_climate_emergency_states` | `src/tiles/runtime/tile_renderer.cpp:133` | 13.160 | 6.016 | entfällt: Notfallpuffer nur für den Fall, dass PSRAM **und** interne Allokation scheitern (`:135-163`); in `.data`, kostet zusätzlich Flash |
| Binary Sensor `g_states` | `src/types/binary_sensor/renderer.cpp:84` | 11.200 | 5.120 | ja |
| Binary Sensor `g_queue` | `src/types/binary_sensor/renderer.cpp:86` | 4.352 | 4.352 | ja (Ringpuffer, kein ISR-Zugriff) |
| Binary Sensor `g_widgets` | `src/types/binary_sensor/renderer.cpp:83` | 2.800 | 1.280 | ja |
| `screensaverConfig` (enthält `TileGridConfig tile_grid_`) | `src/ui/screensaver/screensaver_config.cpp:139`, `.h:63` | 7.936 | 3.800 | ja (Grid als PSRAM-Zeiger) |
| `tileConfig` (enthält `TileGridConfig active_grid`) | `src/tiles/config/tile_config.cpp:324`, `.h:655` | 6.612 | 3.040 | ja |
| Wetter-Widgets `g_tab{0,1,2}_weather` | `src/tiles/runtime/tile_renderer.cpp:103-105` | 0 (P4: PSRAM) | 13.440 | ja, P4 macht es bereits (`tile_renderer_init_cold_storage`, `:165`) |
| Entity-Cache `g_entity_cache` | `src/ui/tabs/tiles/tab_tiles_unified.cpp:349` | 0 (P4: PSRAM) | 5.632 | ja, P4 macht es bereits (`:353-377`) |
| Media-Widgets `g_*_media` | `src/tiles/runtime/tile_renderer.cpp:107-110` | 0 (P4: PSRAM) | 5.120 | ja, wie P4 |
| Climate-Widgets `g_tab{0,1,2}_climate` | `src/tiles/runtime/tile_renderer.cpp:127-129` | 7.980 | 3.648 | ja |
| Switch-Zustände `g_*_switch_states` | `src/tiles/runtime/tile_renderer.cpp:113-116` | 5.040 | 2.304 | ja |
| Cover-Zustände `g_*_cover_states` | `src/tiles/runtime/tile_renderer.cpp:122-125` | 4.480 | 2.048 | ja |
| Sensor-Widgets `g_*_sensors` | `src/tiles/runtime/tile_renderer.cpp:63-66` | 4.480 | 2.048 | ja |
| Cover-Widgets `g_*_covers` | `src/tiles/runtime/tile_renderer.cpp:118-121` | 3.360 | 1.536 | ja |
| Switch-Widgets `g_*_switches` | `src/tiles/runtime/tile_renderer.cpp:68-71` | 1.680 | 768 | ja |
| `g_switch_queue` | `src/tiles/runtime/tile_renderer.cpp:704` | 3.072 | 3.072 | ja |
| `g_update_queue` (Sensor) | `src/tiles/runtime/tile_renderer.cpp:538` | 1.280 | 1.280 | ja |
| `g_media_queue`, `g_weather_queue`, `g_climate_queue`, Cover-`g_queue` | `tile_renderer.cpp:2923, 2533, 2467`, `src/types/cover/renderer.cpp:32` | 1.728 | 1.728 | ja |
| `hardwareIo` | `src/io/hardware_io.cpp:322` | 1.496 | 1.496 | teilweise |
| `haBridgeConfig` (50 `String`-Köpfe) | `src/network/bridge/ha_bridge_config.cpp:56` | 1.000 | 1.000 | Köpfe klein; Inhalte siehe 3.8 |
| `wifi_scan_results` | `src/ui/tabs/settings/tab_settings.cpp:117` | 912 | 912 | ja, nur bei offenem WLAN-Scan nötig |
| `configManager` | `src/core/config/config_manager.cpp:12` | 912 | 912 | teilweise |
| `g_radius_styles` (`lv_style_t`) | `src/ui/shared/ui_surface_style.cpp:26` | 768 | 768 | ja (LVGL-Styles dürfen im PSRAM liegen) |
| `g_ota_upload_state` | `src/web/server/handlers/web_admin_ota.cpp:50` | 768 | 768 | ja, nur während Upload nötig |
| Local-Camera-Zustand (mehrere) | `src/video/local_camera/local_camera.cpp:247, 480, 1375-1798` | 2.356 | 8 | ja, nur bei aktivierter Kamera |
| Summe eigener Code (alle Symbole) | | **95.992** | **79.654** | |

Hinweis zur Skalierung: Die Kachel-Arrays wachsen mit `TILES_PER_GRID`
(`src/tiles/config/tile_config.h:16`). 1280×800-Geräte haben 35 Slots, 1024×600- und
480×480-Geräte weniger; daher kommen die CI-Unterschiede bei „Global variables“
(95,5–136,4 KiB, Tabelle 6.1).

Fremdcode im statischen DRAM (nur Kontext, nicht per HomeTiles-Code änderbar):
FreeRTOS 5,3 KiB (davon ISR-Stack 4.192 B), lwIP 4,3 KiB, Coredump-Stack 1,8 KiB,
mDNS 2,3 KiB (`packet` 1.460 B), auf dem S3 WLAN-Blobs (~14 KiB ohne Debug-Info) und
**`esp32-camera`/`jpge` 7,8 KiB – nur für den Screenshot** (siehe 4.6).

### 3.3 IRAM-Code (gemessen)

| Posten | P4 | S3 | Bemerkung |
|---|---|---|---|
| LVGL Blend/Maske (`LV_ATTRIBUTE_FAST_MEM IRAM_ATTR`, `lv_conf.h:606`) | 51.368 | 53.761 | Blend-Ziele für AL88 (3,6/4,1 KiB), L8 (3,6/4,0 KiB), ARGB8888-premultiplied (3,9/4,2 KiB) werden nie benutzt; die Anzeige ist RGB565 (Tab5: RGB565 swapped, `src/core/display/display_manager.cpp:876-879`) |
| RMT-Treiber (S3) | – | 3.046 | wird über `digitalWrite` → `rgbLedWrite` gezogen, weil die generische S3-Variante `RGB_BUILTIN` (GPIO 48) definiert; HomeTiles nutzt keine RGB-LED |
| eigener Code (`display_manager.cpp`, Gerätetreiber) | 842 | 707 | nötig (ISR-Callbacks) |

### 3.4 Task-Stacks

| Task | Stack | Ort | Quelle | Geräte | PSRAM möglich? |
|---|---|---|---|---|---|
| `loopTask` (LVGL, WebServer, Flash-I/O) | 16 KiB | intern | `HomeTiles.ino:58` | alle | nein: macht Flash-Schreibzugriffe (Cache aus → PSRAM-Stack unzulässig); Größe nach Hochwassermarke prüfen (S3-Diagnose loggt `loop_stack_hwm`) |
| `buildUI` | 24 KiB, nur beim Boot | intern | `HomeTiles.ino:828` | alle | nein (liest LittleFS); unkritisch, da danach frei |
| `mqttWorker` | 12 KiB | PSRAM | `HomeTiles.ino:917` | alle | bereits |
| `media_cover` | 16 KiB | PSRAM | `src/tiles/runtime/tile_renderer.cpp:3948` | alle | bereits |
| `cameraJpeg` | 16 KiB | PSRAM | `src/video/camera_stream.cpp:1366` | P4 | bereits |
| `localCam` / `localCamUp` | je 8 KiB | PSRAM | `src/video/local_camera/local_camera.cpp:2269`, `local_camera_upload.cpp:453` | P4 mit Kamera | bereits |
| `usb_eth_host` / `_client` / `_worker` | 4 + 4 + 7 KiB | intern | `src/network/transport/usb_ethernet_backend.cpp:374-378` | P4, nur mit USB-Ethernet | teilweise (Treiber-Callbacks prüfen) |
| `arduino_events` | 4 KiB | intern | Arduino `NetworkEvents.cpp:15` (per `-DARDUINO_NETWORK_EVENT_TASK_STACK_SIZE` überschreibbar) | alle | nein; ggf. 3 KiB nach Messung |
| mDNS | 4 KiB | intern | `CONFIG_MDNS_TASK_STACK_SIZE` | alle | nein |
| `esp_timer` 8 KiB, `tiT` 4 KiB, `sys_evt` 2 KiB, Timer 2/3 KiB, IDLE 2×1 KiB, IPC 2×1 KiB | – | intern | sdkconfig | alle | nicht änderbar |
| ESP-Hosted (RPC 4 KiB, weitere à 3 KiB) | – | intern | sdkconfig | P4 | nicht änderbar |

Bewertung: Die eigenen Worker-Tasks sind bereits vorbildlich im PSRAM. Übrig bleibt
nur `loopTask`: 0–4 KiB (Schätzung), falls die Hochwassermarke es zulässt.

### 3.5 Queues

| Queue | Speicher | Quelle | Bemerkung |
|---|---|---|---|
| MQTT inbound 64 × Zeiger | ~0,3 KiB intern | `src/network/mqtt/mqtt_handlers.cpp:1561,1569` | Nachrichten selbst im PSRAM – gut |
| MQTT publish/large/control 128/32/128 × Zeiger | ~1,2 KiB intern | `src/network/network_manager.cpp:135-137,1054-1069` | Kommandos selbst im PSRAM – gut |
| Media-Cover Request/Result je 1 × ~520 B | ~1,1 KiB intern | `src/tiles/runtime/tile_renderer.cpp:3939-3942` | Items enthalten `char url[512]`; `xQueueCreateWithCaps(..., MALLOC_CAP_SPIRAM)` würde es verschieben |
| USB-Ethernet 8 × Event, 12 × Zeiger | < 0,3 KiB | `usb_ethernet_backend.cpp:337-338` | vernachlässigbar |
| Eigene Ringpuffer pro Kacheltyp (statisch) | 10,2 KiB intern | siehe 3.2 | größter Posten; siehe Maßnahmen R6/F7 |

### 3.6 LVGL

| Posten | Ort | Quelle | Bemerkung |
|---|---|---|---|
| LVGL-Heap 12 MiB (S3: 2 MiB), Bild-Cache 6 MiB (S3: 512 KiB) | PSRAM | `lv_conf.h:72-95, 518-527`, `lvgl_psram_alloc.cpp` | Fallback auf intern nur bei PSRAM-Fehler – in Ordnung |
| Zeichenband bis 72 KiB | intern DMA | `src/core/display/display_manager.cpp:158-160, 297-330` | bewusst schnell; nach Fragmentierung Fallback auf PSRAM-Doppelpuffer (`:333-354`) – das ist eine konkrete Ursache für langsamere Ordner nach langer Laufzeit |
| Reverse-Flush-Puffer 16 Spalten | intern DMA | `display_manager.cpp:210-227` | nie aufgerufen (`setReverseFlush*` ohne Aufrufer), vom Linker entfernt |
| `LV_DRAW_LAYER_SIMPLE_BUF_SIZE` 24 KiB | PSRAM (LVGL-Heap) | `lv_conf.h:163` | in Ordnung |
| Blend-Code im IRAM | intern | siehe 3.3 | Maßnahme R8 |

### 3.7 Netzwerk und Kamera

| Posten | P4 | S3 | Quelle | Bemerkung |
|---|---|---|---|---|
| MQTT-Paketpuffer 16–32 KiB | PSRAM | PSRAM | `src/network/vendor/pubsubclient/PubSubClient.cpp:20-30` | bereits PSRAM-first |
| MQTT-DMA-Reserve 12 KiB | intern DMA | – | `src/network/network_manager.cpp:77,154` | bewusste Reserve für ESP-Hosted, wird unter Druck freigegeben |
| TLS GitHub-Check | PSRAM-first | intern-first (bewusst: PSRAM-Bus teilt sich mit RGB-Scanout) | `src/core/firmware/github_update.cpp:130-146, 720` | S3: ~40–50 KiB intern (Schätzung) während des Checks |
| TLS GitHub-Install | **Core-Default = intern** | PSRAM-first | `github_update.cpp:436-439` (Scope nur S3) | P4: ~40–50 KiB intern (Schätzung) während der Installation |
| TLS Media-Cover (HTTPS) | gesperrt | Core-Default = intern | `src/tiles/runtime/tile_renderer.cpp:3655-3706` | S3: startet nur bei ≥ 96 KiB freiem internem Speicher (`:3655-3660`), belegt ihn dann für die Sitzung |
| WebServer-/Socket-Puffer | intern (≤ 4 KiB) | intern | Arduino | P4: TCP-Fenster 64 KiB; Uploads setzen bereits `SO_RCVBUF` (`web_admin_files.cpp:762`, `web_admin_ota.cpp:552`) |
| Kamera-Framebuffer, JPEG-Puffer | PSRAM | – | `src/video/camera_stream.cpp:185, 941`, `local_camera.cpp:1119` | bereits PSRAM |
| 4-KiB-TJPGD-Arbeitspuffer | intern (≤ 4096) | intern | `src/ui/screensaver/image_screensaver.cpp:459, 680`, `src/web/server/handlers/web_admin_files.cpp:331` | kurzlebig; mit `MALLOC_CAP_SPIRAM` verschiebbar |

### 3.8 Dynamische Kleinallokationen (interner Heap, Schätzung)

Das sind die Allokationen, die sich über lange Laufzeit zwischen die langlebigen
Blöcke schieben:

1. **Arduino-`String`**: Die Disassembly zählt im eigenen P4-Code **16.120 Aufrufe von
   `String`-Methoden** (4.696 Destruktoren, 1.912 Konstruktoren aus Literalen, 1.744
   `concat`), das sind 49 % aller 32.881 Aufrufstellen (gemessen). Jede Zeichenkette
   über 15 Zeichen (SSO-Grenze) ist ein interner `malloc`. Langlebig betroffen:
   `Tile` mit 8 `String`-Feldern pro Slot (`src/tiles/config/tile_config.h:94-140`),
   `HaBridgeConfigData` mit 50 `String`-Feldern (`src/network/bridge/ha_bridge_config.h:65-93`) –
   Listen zwischen 16 Byte und 4 KiB liegen dauerhaft intern. Im selben Header gibt es
   bereits `PsString` (PSRAM-`std::basic_string`, `:40`).
2. **ArduinoJson 7**: Speicher kommt in Pools zu 1 KiB (`ARDUINOJSON_POOL_CAPACITY` 128)
   über `malloc` – also intern. Betroffen sind z. B. die großen Dokumente in
   `src/types/value/value_control.cpp:68`, `src/types/energy/energy_data.cpp:203` und
   `src/ui/popups/sensor/sensor_popup.cpp:2969` (die Kapazitätsangabe von
   `DynamicJsonDocument` ignoriert v7).
3. **`Serial.printf`**: Arduinos `Print::vprintf` (`cores/esp32/Print.cpp:46-71`)
   formatiert in einen 64-Byte-Stackpuffer und ruft bei längeren Zeilen
   `malloc(len+1)` auf – intern. Im P4-Binary sind 116 Log-Formate schon ohne Argumente
   ≥ 64 Zeichen, 215 Formate mit Argumenten ≥ 48 Zeichen (gemessen).

---

## 4. Refactoring des eigenen Codes (Flash)

### 4.1 Gesamtbild (gemessen)

| | P4 Waveshare 8" | S3 4848S040 |
|---|---|---|
| Funktionen aus eigenen Quellen (`nm`, inkl. eingebetteter Inline-Teile) | 812.950 B | 732.290 B |
| ArduinoJson-Template-Code (Header-Funktionen) | 39.532 B | 33.699 B |
| libstdc++-Templates (`std::vector`, `std::function`, `std::sort` …) | 32.462 B | 26.540 B |
| `.gcc_except_table` aus eigenem Code | 47.487 B | 44.802 B |
| `.eh_frame` aus eigenem Code | 148.588 B | 86.200 B |

Die größten eigenen Dateien (Code, P4): `tile_renderer.cpp` 43,8 KiB,
`weather_popup.cpp` 39,5 KiB, `sensor_popup.cpp` 39,1 KiB, `web_admin_html.cpp`
27,0 KiB, `tile_config.cpp` 26,7 KiB, `climate_popup.cpp` 24,2 KiB,
`tab_settings.cpp` 23,8 KiB, `ha_bridge_config.cpp` 23,6 KiB,
`local_camera.cpp` 23,1 KiB, `mqtt_handlers.cpp` 22,8 KiB.

### 4.2 Aufblähung durch Sprachfeatures, Templates und Inline-Funktionen

**F1 – Exceptions.** Die Arduino-Flags setzen `-fexceptions`; im eigenen Code gibt
es kein `try`, `catch` oder `throw`, und `CONFIG_ESP_SYSTEM_USE_EH_FRAME` ist aus (die
Unwind-Tabellen dienen also nicht einmal dem Panic-Backtrace; Offline-Analyse nutzt
`.debug_frame` aus der ELF). Ein nicht abgefangenes `bad_alloc` endet heute wie
später in `std::terminate`. Messbuild mit `-fno-exceptions` für C++:

| | P4 Waveshare 8" | S3 4848S040 |
|---|---|---|
| App-Image vorher → nachher | 6.586.704 → 6.284.608 B | 5.474.224 → 5.156.656 B |
| **Einsparung gesamt** | **−302.096 B = −295,0 KiB** | **−317.568 B = −310,1 KiB** |
| `.flash.text` (Landing-Pads) | −72.276 B | −132.608 B |
| `.flash.rodata` (`.gcc_except_table`; S3 inkl. `.eh_frame`) | −51.304 B | −184.960 B |
| `.eh_frame` (P4 eigene Sektion) | 190.972 → 12.460 B (−178.512 B) | in `.flash.rodata` enthalten |
| statischer RAM | unverändert | unverändert |

Die Messung umfasst den gesamten aus Quelltext gebauten C++-Code (Firmware, Arduino-Core,
Arduino-Bibliotheken); die vorkompilierten ESP-IDF-Bibliotheken bleiben unverändert.
Beide Builds kompilieren fehlerfrei, es gibt also auch im mitgebauten Fremdcode kein
`try`/`throw`. Nicht gemessen: Tab5 (M5GFX/M5Unified) und 4B (Arduino_GFX auf P4).
Umsetzung: `-fno-exceptions` muss **nach** der SDK-Flagdatei stehen;
`compiler.cpp.extra_flags` wirkt nicht, weil es in `recipe.cpp.o.pattern` davor
steht (ein erster Messbuild damit blieb byte-gleich). Zu testen: jede Stelle mit
`new` ohne `std::nothrow` (bereits 11 `nothrow`-Stellen vorhanden). Aufwand S,
Risiko niedrig–mittel (Build-Konfiguration für alle 17 Profile, CI-Flags anpassen).

**F2 – Arduino-`String` im Code.** 16.120 der 32.881 Aufrufstellen im eigenen P4-Code
gehen an `String` (gemessen). Eine Aufrufstelle kostet 6–12 Byte plus
Argumentaufbereitung; das sind grob 100–150 KiB Code (Schätzung). Die häufigsten
Muster und Gegenmittel:

- `const String&`-Parameter, die mit Literalen aufgerufen werden, erzeugen temporäre
  `String`-Objekte (1.912 `String(const char*)`-Aufrufe) → `const char*`-Überladungen,
  wie sie `HaBridgeConfig::find*` schon anbietet (`ha_bridge_config.h:111-121`).
- `html += "…"`-Ketten: 1.373 Verkettungen in `web_admin_html.cpp` und
  `types/*/web_html.cpp` → siehe F3.
- `String` als Rückgabewert in heißen Pfaden (`find*`-Methoden, Parser) → Ausgabe in
  Aufruferpuffer.

Realistisch schrittweise: 40–80 KiB Flash (Schätzung) und deutlich weniger interne
Heap-Allokationen (3.8). Aufwand L, Risiko mittel.

**F4 – ArduinoJson-Klone.** Dieselben ArduinoJson-Funktionen existieren als
`constprop`/`isra`-Klone in mehreren Übersetzungseinheiten: 11,2 KiB (P4) bzw.
9,4 KiB (S3) doppelter Code (gemessen), z. B. `JsonDeserializer::parse…` sechsfach,
`ObjectData::getMember` zwölffach. Dazu kommen drei eigene JSON-Scanner (4.3).
Eine gemeinsame, nicht-templatisierte JSON-Schicht in einer einzigen `.cpp` (Parsen
mit Filter, Feldzugriff, Escaping) beseitigt die Klone. 10–15 KiB (Schätzung), Aufwand M, Risiko niedrig–mittel.

**F10 – `std::sort`.** Sechs Instanziierungen (`__introsort_loop`,
`__adjust_heap`, `__insertion_sort` …) belegen 6,9 KiB (gemessen). Ein gemeinsamer
Sortierhelfer mit Vergleichsfunktion spart ~4 KiB (Schätzung), Aufwand S, Risiko
niedrig.

**Header-Inline-Code und Sondermember.** Funktionen aus eigenen Headern:
`tile_icon_colors.h` 6,4 KiB, `ha_bridge_config.h` 5,9 KiB, `tile_config.h`
4,8 KiB, `camera_indicator.h` 2,8 KiB (P4, gemessen). Echte Mehrfachkopien davon nur
1,4 KiB. Größer sind die implizit erzeugten Kopier-/Zuweisungs-/Destruktorfunktionen
für `String`-lastige Strukturen: `HaBridgeConfigData` 5,8 KiB, `Tile` 2,7 KiB,
`*PopupInit` zusammen 5,4 KiB, Popup-Kontexte 5,0 KiB (gemessen, zusammen 19,5 KiB).
Übergabe per `const&`/Move statt Kopie und schlankere Felder (feste Puffer bzw.
`PsString`) sparen davon geschätzt 5–8 KiB, Aufwand M, Risiko mittel.

### 4.3 Doppelte Logik zwischen Kacheltypen und Popups

Messgrundlage: Funktionsfamilien per Namensmuster aus `nm` (P4, gemessen); die
Einsparung ist geschätzt, weil nur der doppelte Anteil entfällt.

| Familie | Umfang P4 (gemessen) | Befund | Vorschlag | Einsparung | Aufwand | Risiko |
|---|---|---|---|---|---|---|
| Update-Queues pro Kacheltyp | 6,2 KiB Code in 7 Implementierungen + 10,2 KiB statische Ringpuffer | Sensor, Switch, Climate, Weather, Media, Cover und Binary Sensor haben je eigenes `queue_*`/`process_*_queue` mit gleicher Struktur; `process_climate/weather/cover_update_queue` sind zu 74 % instruktionsgleich, `queue_climate_tile_update`/`queue_cover_tile_update` zu 86 % | eine generische Queue (Typ-ID + Slot + Payload im PSRAM-Pool) | 2–4 KiB Flash + bis 10 KiB interner RAM | M | niedrig–mittel |
| Popup-Gerüst | 62,8 KiB in 13 Dateien (`show_/preload_/finish_/update_*_popup`, `apply_init`, Kontexte, Callbacks) | trotz `popup_shell.cpp` hat jedes Popup eigene `on_overlay_delete` (6×), `on_overlay_click` (6×), `on_close_click`/`on_close` (8×), `remote_apply_timer_cb`/`live_publish_timer_cb` (je 2×) – 27 Callbacks, 3,4 KiB; `*PopupInit` wird per Wert kopiert | gemeinsamer Popup-Lebenszyklus (Overlay, Schließen, Timer, Init-Übernahme) in `popup_open.cpp`/`popup_shell.cpp` | 5–10 KiB | M | mittel (AGENTS §6: Lebenszyklus, Preload, Sleep) |
| Payload-Parsing pro Typ | 24,1 KiB in 3 Dateien | `update_media_tile_state` 5,7 KiB, `update_weather_tile_state` 4,3 KiB, `parse_switch_payload` 2,6 KiB, Climate-Lambda 1,6 KiB – jeweils eigenes Feld-Scannen | ein tabellengetriebener Feld-Extraktor (auf `src/core/json_scan.h` aufbauen) | 3–6 KiB | M | mittel |
| Graph/Verlauf | 28,4 KiB in 10 Dateien | Sensor-Popup (Zeitachse, Verlauf), Energy-Chart, Wetter-Prognosegraph und Kachelgraphen berechnen Achsen/Readouts getrennt; `popup_graph_readout.h` ist nur teilweise geteilt | gemeinsame Zeitachsen-/Skalierungs-/Readout-Hilfen | 3–6 KiB | M | mittel |
| Editable-Typen Number/Select/DateTime | `append_*_fields_html` 3 × 656 B, `apply_*_fields_from_request` 3 × 564 B, `render_*_tile` 3 × 414 B | 53–73 % instruktionsgleich, die Typen teilen laut Projektkontext bereits Persistenz und Popup | eine parametrisierte Implementierung | ~2,5 KiB | S | niedrig |
| JSON-Handhelfer | 5,8 KiB in 7 Dateien | `extract_json_string_field` 3× (`tile_renderer.cpp:814`, `weather_popup.cpp:452`, `mqtt_handlers.cpp:328`), `decode_basic_json_escapes` 2× (`tile_renderer.cpp:1010`, `weather_popup.cpp:443`), `appendJsonEscaped` 3× (`hardware_io.cpp:122`, `ha_bridge_config.cpp:362`, `web_admin_handler_utils.cpp:14`), `findMatchingJson*End` 2× | alles nach `src/core/json_scan.h` | 2–3 KiB (Teil von F4) | S | niedrig |
| JPEG-Ausgabe-Callbacks | 3 × 192 B | `media_cover_jpeg_output` (`tile_renderer.cpp:3149`), `icon_jpeg_output` (`types/scene/renderer.cpp:43`), `sw_jpeg_output` (`image_screensaver.cpp:435`) 64–71 % gleich | ein gemeinsamer TJPGD-Wrapper | ~0,5 KiB | S | niedrig |
| Kleinkram | je 0,2–0,5 KiB | `parse_iso_date` 2× (`tile_renderer.cpp:1141`, `weather_popup.cpp:1066`); `writeImagePathSd`/`writeLongEntityIdSd` (`tile_config.cpp:782/843`) und `readLongTitleSd`/`readIconColorsSd` (`:883/955`) je 61–70 % gleich; `mqttPublishClimateHvacMode`/`…PresetMode` (`mqtt_handlers.cpp:2190/2210`) | zusammenlegen | ~1,5 KiB | S | niedrig |

### 4.4 Toter und selten genutzter Code

| Posten | Umfang (gemessen) | Bewertung | Einsparung | Aufwand | Risiko |
|---|---|---|---|---|---|
| Animation-Kachel (`src/types/pixelanim/`) | P4 8,7 KiB, S3 9,9 KiB Flash (Renderer 3,9/4,5, Web-HTML 4,1/4,5, Handler 0,8/0,9) + ~3 KiB Quelltext im Admin-JS; kein statischer RAM | klein; Entfernen verlangt reservierte Typ-ID, Import/Export- und Migrationspfade (AGENTS §5.1) | ~9–10 KiB | M | mittel |
| Reverse-Flush (`display_manager.cpp:37, 210-270`) | 0 B im Binary (vom Linker entfernt) | reiner Quelltext-Ballast | 0 KiB | S | keins |
| Legacy-/Migrationscode (V6-Grids, alte Icon-Farbregeln, alte HW-IO-IDs, SD-Migration) | P4 2,3 KiB, S3 1,9 KiB | Kompatibilität wichtiger als 2 KiB | – | – | – |
| USB-Ethernet-Backend | auf 8"/S3 nur 0,1–0,5 KiB gelinkt | nur auf Geräten mit USB-Host relevant | – | – | – |
| Eingebaute Kamera (`src/video/local_camera/*`, Upload, Web-Handler, Sensoren, Presenter) | P4 ~53 KiB Flash, 2,5 KiB statischer RAM | opt-in, aber in jedem Release-Image der Kamera-Boards; der statische Zustand (2,3 KiB) ließe sich erst bei Aktivierung anlegen | 2,3 KiB interner RAM | S | niedrig |
| Hardware-IO (`src/io/hardware_io.cpp`) | P4 32,4 KiB, S3 31,6 KiB Flash, 1,5 KiB statischer RAM | Nischenfunktion (Relais und DS18x20 über 1-Wire an freien GPIOs); Schalter pro Profil denkbar | bis ~30 KiB Flash auf Profilen ohne Hardware-IO | M | mittel |

### 4.5 Debug-/Diagnose-Code und Log-Texte

Gemessen im P4-Binary:

- **811 Aufrufstellen** von `Print::printf/println/print` (484 `printf`, 286 `println`,
  63 `print`), im Mittel **24 Byte** Code je Stelle (gemessen an 469 Stellen) → ~19 KiB
  Code, dazu 79 `log_printf`-Aufrufe.
- **931 Log-Literale** aus dem Quelltext wörtlich im Binary gefunden, zusammen
  **45.409 B**; die meisten nach Datei: `network_manager.cpp` 4,0 KiB,
  `local_camera.cpp` 3,8 KiB, `mqtt_handlers.cpp` 3,4 KiB, `HomeTiles.ino` 2,9 KiB,
  `device_waveshare_touch_lcd_8.cpp` 2,9 KiB, `tile_renderer.cpp` 2,7 KiB,
  `web_admin_ota.cpp` 2,4 KiB, `tile_config.cpp` 2,2 KiB, `camera_stream.cpp` 2,1 KiB.
- Die längsten Formate sind 110–179 Zeichen: `[Mem] … Heap free=…` (`HomeTiles.ino:102`),
  `[Tiles] folder-cache evict …` (`tab_tiles_unified.cpp`),
  `[Bridge] Configuration received …` (`ha_bridge_config.cpp`),
  `[LocalCam] Snapshot …`, `[LoopGap] total=…` (`HomeTiles.ino`).
- Explizite Diagnosefunktionen (Screenshot, SD-Diagnose, Coredump-/Crashlog-Download,
  Wedge-Report, OTA-Diagnose): 11,6 KiB (P4) / 8,4 KiB (S3) – nützlich, behalten.

Vorschläge:

| # | Maßnahme | Einsparung | Aufwand | Risiko |
|---|---|---|---|---|
| F5a | Eigener Log-Helfer (`ht_log(level, fmt, …)`) mit festem 256-Byte-Puffer und direktem `write` – ersetzt `Serial.printf` und vermeidet den internen `malloc` pro langer Zeile (3.8) | 0 KiB Flash, weniger Heap-Wechsel | M | niedrig |
| F5b | Compile-Zeit-Stufe `HT_LOG_VERBOSE` für Messlogs (Zeitmessungen, Speicherstände, Cache-Statistik, Kamera-Tuning), in Release-Builds aus | 15–25 KiB (Schätzung: ~40 % der Stellen und Texte) | M | mittel: weniger Felddiagnose; Fehler-/Warnlogs bleiben |
| F5c | Lange Formate kürzen (`key=value`, keine Prosa, Einheiten einmal) | 5–10 KiB (Schätzung: 20 % von 45 KiB) | S | niedrig; Präfixe bleiben stabil (AGENTS §3) |

### 4.6 Bibliotheken mit ungünstigem Kosten/Nutzen

| Bibliothek | Umfang (gemessen) | Nutzen in HomeTiles | Vorschlag | Einsparung | Aufwand | Risiko | Geräte |
|---|---|---|---|---|---|---|---|
| LVGL-Widgets ohne Verwendung (Scale, Table, Menu, Calendar ×3, Tabview, Checkbox, LED, Tileview, Msgbox, Win, List, Span; GIF, Themes Simple/Mono) | Objektdateien 18,7 KiB (Map), gelinkt über Theme-Referenzen | keiner (0 Aufrufe; der Messbuild kompiliert ohne sie). Das Grid-Layout wird dagegen genutzt (`lv_obj_set_grid_cell`, `tile_renderer.cpp:4737`) | in `lv_conf.h:753-879` abschalten | ~30 KiB Flash (P4-8"; gemessen zusammen mit den Farbformaten aus 5.3: −51,0 KiB Image) | S | niedrig (Compiler meldet jede Nutzung) | alle |
| NimBLE-Host über ESP-Hosted (`libbt`) | P4 49,8 KiB Flash | keiner; `vhci_drv.c` aus ESP-Hosted referenziert `ble_transport_*` | die ohnehin selbst gebauten ESP-Hosted-Objekte (`tools/esp-hosted-3.3.7-*`) ohne BT bauen oder Symbole stubben | ~50 KiB | M | mittel | P4 |
| Arduino_GFX | S3 38,6 KiB | nur ST7701-Init-Sequenz und RGB-Wrapper (`device_guition_esp32_4848s040.cpp:205ff`); Panel läuft über `esp_lcd_new_rgb_panel` | eigene 3-Wire-Init + `esp_lcd` direkt | 30–35 KiB (Schätzung) | M–L | mittel–hoch (Panel-Init) | S3, 4B |
| M5GFX + M5Unified | Tab5 42,3 + 29,2 KiB Flash, 2,3 KiB DRAM | Display/Touch/Power | nativer DSI-Pfad wie andere P4-Profile | 55–65 KiB (Schätzung) | L | hoch | Tab5 (Flash-kritisch!) |
| LodePNG | P4 26,3 / S3 24,5 KiB | nur PNG-Bilder in Szenen-Kacheln (`types/scene/renderer.cpp:198-263`) | PNG beim Upload im Browser in JPEG/LVGL-Binärformat wandeln | ~25 KiB | M | mittel (vorhandene SD-Dateien) | alle |
| `esp32-camera` (nur `fmt2jpg`) | S3 9,8 KiB Flash + **7,8 KiB statischer DRAM** (`jpge`-Tabellen) | Screenshot (`web_admin_diagnostics.cpp:23,108`) | Rohdaten/BMP senden, PNG im Browser erzeugen | 9,8 KiB Flash + 7,8 KiB interner RAM | M | niedrig | S3 |
| RMT-Treiber | S3 13,9 KiB Flash + 3,0 KiB IRAM | keiner (Nebenwirkung von `RGB_BUILTIN` in der generischen S3-Variante) | eigene Board-Variante ohne `RGB_BUILTIN` (`build.variant.path`) | 13,9 KiB Flash + 3,0 KiB IRAM | S–M | niedrig | S3 |
| HTTPClient | P4 11,4 / S3 13,4 KiB | Update-Check (`github_update.cpp:749`) und Media-Cover (`tile_renderer.cpp:3693`) | eigener Minimal-HTTP-Pfad existiert schon (`fetchHttpRange`, Kamera-Stream) | ~10 KiB | M | mittel (Redirects) | alle |
| mDNS | 33,4 KiB Flash, 2,3 KiB DRAM, 4 KiB Task | wird genutzt (`network_manager.cpp:1929`) | behalten | – | – | – | – |

### 4.7 Serverseitig zusammengesetztes Web-HTML

Die Web Admin baut Formulare in C++ aus Literalen zusammen:
`src/web/server/render/*` 84,8 KiB, `src/types/*/web_html.cpp` 33,1 KiB,
`web_handler.cpp` 13,4 KiB, `web_scripts.cpp` 5,6 KiB, Setup-Portal 12,7 KiB – zusammen
**149,6 KiB (P4) / 152,5 KiB (S3)**, davon ~70 KiB unkomprimierte Strings und ~79 KiB Code
(1.373 `+=`-Verkettungen) (gemessen, Strings vor Zusammenführung). Dasselbe Markup als
Template im ohnehin gzip-komprimierten Admin-Bundle (`src/web/generated/`) kostet etwa
ein Viertel. Vorschlag F3: typspezifische Formulare clientseitig rendern, Server
liefert nur JSON. **90–120 KiB** (Schätzung), Aufwand L, Risiko mittel (Browser-
Vertragstests nach AGENTS §5.4 vorhanden). Betrifft alle Geräte, wirkt am stärksten
auf dem Tab5.

---

## 5. Einzelbefunde, die RAM und Flash zugleich betreffen

### 5.1 `g_climate_emergency_states` (R1)
`src/tiles/runtime/tile_renderer.cpp:133` hält `TILES_PER_GRID` `ClimateState`
(376 B je Slot) als Notfallreserve, falls `allocate_climate_states()` weder PSRAM noch
internen RAM bekommt (`:135-163`). Weil `ClimateState` Initialisierer hat, liegt das
Array in `.data`: 13.160 B intern **und** 13.160 B im Flash-Image (P4-8"). Ohne
Speicher lässt sich ohnehin keine Climate-Kachel sinnvoll bauen; ein sauberer
Fehlerpfad oder ein einzelner Slot reicht.

### 5.2 S3 ohne „cold storage“ (R2)
`tile_renderer_init_cold_storage()` (`tile_renderer.cpp:165-214`) und
`ensure_entity_cache_storage()` (`tab_tiles_unified.cpp:353-377`) legen Wetter-/Media-
Widgets und den Entity-Cache auf dem P4 in PSRAM an (Release v0.6.5: „moved cold
renderer bookkeeping into PSRAM to reclaim internal RAM“). Auf dem S3, dem Chip mit
weniger internem RAM, bleiben 24.192 B statisch intern. Die Zugriffe sind selten
(Zustandswechsel), die PSRAM-Bandbreite gegenüber dem RGB-Scanout daher unkritisch;
trotzdem auf Hardware prüfen.

### 5.3 LVGL-Farbformate (R8)
`lv_conf.h:193-203` aktiviert alle SW-Farbformate. HomeTiles nutzt RGB565,
RGB565-swapped, ARGB8888, RGB888 (TJPGD-Ausgabe) und I1 (QR-Code); A8 wird intern für
Transformationen gebraucht. Unbenutzt: AL88, L8, ARGB8888-premultiplied.
Messbuild (P4-8", zusammen mit den Widgets aus 4.6): IRAM 131.856 → 110.282 B
(**−21,1 KiB**), App-Image 6.586.704 → 6.534.448 B (**−51,0 KiB**), statischer DRAM
unverändert (135.616 B). Das ist mehr als die drei Blend-Zieldateien allein (11,1 KiB),
weil auch die Quellformat-Zweige in den übrigen Blend-Routinen entfallen. Auf dem S3
ist ein ähnlicher Effekt zu erwarten (Schätzung; dort 52,5 KiB LVGL-IRAM).
Weitergehend: `LV_ATTRIBUTE_FAST_MEM` leer lassen spart die gesamten 50,2 KiB (P4) bzw.
52,5 KiB (S3) IRAM, kostet aber Zeichenleistung aus dem Flash-Cache – nur mit
Zeitmessung auf Hardware (Folder-Wechsel, Popup-Öffnen).

### 5.4 Zeichenband 72 KiB (R14)
`display_manager.cpp:158-160,297-330` belegt bis 72 KiB internen DMA-RAM, aber nur,
solange danach noch 150 KiB interner DMA-Speicher frei bleiben. Nach Kamera-Popup oder OTA wird das Band freigegeben und später neu
angefordert; bei fragmentiertem Heap fällt die UI auf den langsameren PSRAM-
Doppelpuffer zurück (`:333-354`, Kommentar in `camera_popup.cpp:100-114`). Das ist
eine konkrete Ursache für „träge Ordner nach langer Laufzeit“. Optionen: Band
einmalig beim Boot anlegen und nie freigeben (spart nichts, verhindert den Abstieg)
oder Deckel auf 48 KiB senken (−24 KiB, Schätzung; Leistung messen).

### 5.5 TLS (R15/R16)
Der P4-Install-Pfad (`fetchHttpRange`, `github_update.cpp:428-439`) setzt den
PSRAM-Allocator nur für den S3; auf dem P4 läuft die TLS-Sitzung mit dem Core-Default
intern (~40–50 KiB, Schätzung). Der Check-Pfad (`:720`) nutzt auf dem P4 bereits
PSRAM-first. Ausweiten ist wenig Code, muss aber wegen der P4-OTA-Vorgeschichte
(`PROJECT_CONTEXT.md`, „Failed P4 OTA approaches“) auf Hardware getestet werden.
Auf dem S3 ist intern-first beim Check bewusst gewählt; die einzige wirklich
TLS-freie Lösung wäre ein Update über die Bridge (HTTP im LAN).

---

## 6. Kontext: Flash-Reserve und Assets (nicht Schwerpunkt)

### 6.1 Flash-Reserve je Profil (gemessen, CI)
`release-helper/package-ci-build.js:7,135` prüft jedes Update-Image gegen den
OTA-Slot `0x680000` = 6.815.744 B – auch auf 32-MiB-Geräten.

| Profil | Chip | App-Image | Reserve zum OTA-Slot | statisches DRAM |
|---|---|---|---|---|
| M5Stack Tab5 | P4 | 6.703.082 | 112.662 (110 KiB, 98,3 %) | 124.352 |
| Guition JC8012P4A1 | P4 | 6.623.808 | 191.936 (187 KiB, 97,2 %) | 136.492 |
| Guition JC8012P4A1 V2 | P4 | 6.616.294 | 199.450 (195 KiB, 97,1 %) | 136.632 |
| Waveshare 4B | P4 | 6.604.784 | 210.960 (206 KiB, 96,9 %) | 97.792 |
| Waveshare 10.1 (v3.1+) | P4 | 6.592.050 | 223.694 (218 KiB, 96,7 %) | 136.476 |
| Waveshare 10.1 | P4 | 6.590.394 | 225.350 (220 KiB, 96,7 %) | 136.476 |
| Waveshare 8 | P4 | 6.586.102 | 229.642 (224 KiB, 96,6 %) | 135.632 |
| Waveshare 7 | P4 | 6.582.665 | 233.079 (228 KiB, 96,6 %) | 122.980 |
| Guition JC1060P470C V2 | P4 | 6.138.024 | 677.720 (662 KiB, 90,1 %) | 113.772 |
| Waveshare 7B (rev 3.1) | P4 | 6.132.312 | 683.432 (667 KiB, 90,0 %) | 113.508 |
| Waveshare 7B / 7B-C | P4 | 6.130.686 | 685.058 (669 KiB, 89,9 %) | 113.516 |
| Guition JC1060P470C | P4 | 6.040.600 | 775.144 (757 KiB, 88,6 %) | 111.020 |
| Waveshare 4.3 | P4 | 5.721.850 | 1.093.894 (1.068 KiB, 84,0 %) | 106.028 |
| Guition JC4880P443 | P4 | 5.718.858 | 1.096.886 (1.071 KiB, 83,9 %) | 113.712 |
| GUITION ESP32-4848S040 | S3 | 5.473.726 | 1.342.018 (1.311 KiB, 80,3 %) | 139.576 |
| Waveshare S3 4B | S3 | 5.441.906 | 1.373.838 (1.342 KiB, 79,8 %) | 139.640 |
| Waveshare S3 4 Rev 4.0 | S3 | 5.435.351 | 1.380.393 (1.348 KiB, 79,7 %) | 139.504 |

### 6.2 Assets (gemessen, nur zur Einordnung)
- MDI-Iconfont: Pro Build ist **nur eine** Größe gelinkt – 48 px auf 1280×800/Tab5
  (2.220.409 B), 40 px auf 1024×600, 32 px auf 480×480 (1.476.006 B auf dem S3)
  (`src/fonts/mdi_icons_*.c:11-17`). Icon-Namenstabelle 7.448 Einträge:
  59.576 B Zeigertabelle + 120.677 B Namen (`src/tiles/icons/mdi_icons.cpp`).
- Inter-UI-Fonts 16–96 px: 911.809 B ≈ 890 KiB (P4); 16–48 px sind ohne Kompression erzeugt (`--no-compress` im Dateikopf).
  Montserrat 32/40/48 sind aktiviert, aber vom Linker entfernt; nur Montserrat 14
  (14,6 KiB) bleibt als `LV_FONT_DEFAULT`.
- Web-Admin-Bundle gzip: JS 96.825 B, CSS 18.564 B, Inter-WOFF2 48.620 B.
- i18n (de/en/fr): 36–38 KiB inkl. Strings, vollständig im Flash (`const`).
- Startlogo `hometiles_logo.cpp`: 82.972 B.

---

## 7. Maßnahmenliste, sortiert nach Nutzen zu Aufwand

Aufwand: S = bis 1 Tag, M = wenige Tage, L = mehr als eine Woche. Speicher: I-RAM =
interner RAM (DRAM/IRAM), I-Heap = dynamischer interner Heap, F = Flash.

| Rang | ID | Maßnahme (Quelle) | Einsparung | Speicher | Aufwand | Risiko | Geräte |
|---|---|---|---|---|---|---|---|
| 1 | F1 | `-fno-exceptions` für C++ (Build-Flag, Reihenfolge beachten) | 295,0 KiB P4-8" / 310,1 KiB S3 (gemessen) | F | S | niedrig–mittel | alle |
| 2 | R1 | `g_climate_emergency_states` entfernen (`tile_renderer.cpp:133`) | 12,9 KiB P4-8" / 5,9 KiB S3 I-RAM + ebenso viel F (gemessen) | I-RAM, F | S | niedrig | alle |
| 3 | R2 | S3 „cold storage“ wie P4 (`tile_renderer.cpp:103-110,165`; `tab_tiles_unified.cpp:349`) | 23,6 KiB (gemessen) | I-RAM | S | niedrig–mittel | S3 |
| 4 | R8/F11 | LVGL: ungenutzte Widgets/Formate aus (`lv_conf.h:193-203, 753-879`) | 51,0 KiB F, davon 21,1 KiB IRAM (P4-8", gemessen); S3 ähnlich (Schätzung) | F, I-RAM (IRAM) | S | niedrig | alle |
| 5 | R3 | Binary-Sensor-Zustände/-Widgets → PSRAM (`binary_sensor/renderer.cpp:83-84`) | 13,7 KiB P4-8" / 6,3 KiB S3 (gemessen) | I-RAM | S | niedrig | alle |
| 6 | R13 | ArduinoJson mit PSRAM-Allocator (`value_control.cpp:68`, `energy_data.cpp:203`, `sensor_popup.cpp:2969` u. a.) | bis ~30 KiB Spitzenlast (Schätzung) | I-Heap | S | niedrig | alle |
| 7 | R9 | S3: Variante ohne `RGB_BUILTIN` (RMT fällt weg) | 3,0 KiB IRAM + 13,9 KiB F (gemessen) | I-RAM, F | S–M | niedrig | S3 |
| 8 | R7 | Kleine Zustände nur bei Bedarf anlegen: `wifi_scan_results`, `g_ota_upload_state`, Local-Camera-Zustand, Media-Cover-Queues als `…WithCaps` | P4-8" 5,0 KiB / S3 2,7 KiB (gemessen; Queue-Anteil ~1,1 KiB geschätzt) | I-RAM | S | niedrig | alle (Kamera: P4) |
| 9 | R4 | Übrige Kachel-Arrays → ein PSRAM-Block (`tile_renderer.cpp:63-129`) | 26,4 KiB P4-8" / 12,1 KiB S3 (gemessen) | I-RAM | M | niedrig–mittel | alle |
| 10 | R5 | `TileGridConfig` → PSRAM (`tile_config.h:655`, `screensaver_config.h:63`) | 14,2 KiB P4-8" / 6,7 KiB S3 (gemessen) | I-RAM | M | niedrig | alle |
| 11 | R6/F7 | Generische Update-Queue statt 7 Ringpuffern | 10,2 KiB I-RAM (gemessen) + 2–4 KiB F (Schätzung) | I-RAM, F | M | niedrig–mittel | alle |
| 12 | F5a | Log-Helfer mit festem Puffer (kein `malloc` je Zeile) | Heap-Wechsel; 0 KiB F | I-Heap | M | niedrig | alle |
| 13 | R10 | S3-Screenshot ohne `esp32-camera` (`web_admin_diagnostics.cpp:108`) | 7,8 KiB I-RAM + 9,8 KiB F (gemessen) | I-RAM, F | M | niedrig | S3 |
| 14 | F4 | JSON-Schicht bündeln (Klone + Handhelfer) | 10–15 KiB (Schätzung; Klone 11,2 KiB gemessen) | F | M | niedrig–mittel | alle |
| 15 | F5b/c | Verbose-Logs per Compile-Stufe, Formate kürzen | 20–35 KiB (Schätzung) | F | M | mittel | alle |
| 16 | F8/F9/F10 | Kleine Dubletten (Editable-Typen, JPEG-Callbacks, `parse_iso_date`, SD-Sidecars, `std::sort`) | ~8 KiB (Basis gemessen) | F | S | niedrig | alle |
| 17 | F19 | ESP-Hosted ohne NimBLE-Host | ~50 KiB (gemessen: 49,8 KiB `libbt`) | F | M | mittel | P4 |
| 18 | R12 | Langlebige `String`s → `PsString`/feste Puffer (`HaBridgeConfigData`, `Tile`) | 5–30 KiB I-Heap je nach HA-Konfiguration (Schätzung) | I-Heap | M–L | mittel | alle |
| 19 | F6 | Gemeinsamer Popup-Lebenszyklus, Init per Referenz | 5–10 KiB (Schätzung) | F | M | mittel | alle |
| 20 | F15/F16 | HTTPClient und LodePNG ersetzen | ~10 + ~25 KiB (Schätzung; Bibliotheken 11,4–13,4 bzw. 24,5–26,3 KiB gemessen) | F | M | mittel | alle |
| 21 | R15 | P4-Install-TLS PSRAM-first | ~40–50 KiB Spitzenlast (Schätzung) | I-Heap | S | mittel (OTA-Hardwaretest) | P4 |
| 22 | R14 | Zeichenband pinnen oder auf 48 KiB deckeln | 0 bzw. 24 KiB (Schätzung) | I-RAM (DMA) | S | mittel (Leistung) | alle |
| 23 | F3 | Web-Formulare clientseitig rendern | 90–120 KiB (Schätzung) | F | L | mittel | alle, v. a. Tab5 |
| 24 | F2 | `String` schrittweise zurückdrängen | 40–80 KiB F (Schätzung) + weniger I-Heap | F, I-Heap | L | mittel | alle |
| 25 | F18 | Tab5 ohne M5GFX/M5Unified | 55–65 KiB (Schätzung; Bibliotheken 71,5 KiB gemessen) | F | L | hoch | Tab5 |
| 26 | F17 | S3 ohne Arduino_GFX | 30–35 KiB (Schätzung; Bibliothek 38,6 KiB gemessen) | F | M–L | mittel–hoch | S3, 4B |
| 27 | R17 | `loopTask` 16 → 12 KiB nach Hochwassermarke | 0–4 KiB (Schätzung) | I-RAM | S | mittel | alle |
| – | F12 | Animation-Kachel entfernen | 8,7 KiB P4 / 9,9 KiB S3 (gemessen) | F | M | mittel | alle |
| – | R16 | OTA über die Bridge (TLS-frei) | ~40–50 KiB Spitzenlast S3 (Schätzung) | I-Heap | L | mittel | S3 |

Summe der gemessenen, risikoarmen internen RAM-Posten R1–R7, R9 und R10: auf dem
8"-P4 ~82 KiB statischer DRAM, dazu 21 KiB IRAM aus R8 (gemessen); auf dem S3 ~78 KiB
(inkl. 3 KiB IRAM aus R9), dazu R8 (~21 KiB IRAM, Schätzung nach der P4-Messung).

---

## 8. Offene Messpunkte (Hardware)

1. `[Mem] boot-start` / `after-network-init` / `after-mqtt-worker` (`HomeTiles.ino:93-126`)
   vor und nach R1–R6 auf einem 8"-P4 und einem S3: freier interner Heap und größter
   DMA-Block.
2. Hochwassermarken von `loopTask` und `mqttWorker` über 24 h (S3-Diagnose loggt sie
   bereits) als Grundlage für R17.
3. Zeitmessung Folder-Wechsel und Popup-Öffnen für R8 (Farbformate, optional
   `LV_ATTRIBUTE_FAST_MEM`) und R14.
4. S3-RGB-Scanout unter PSRAM-Zugriffen für R2/R4 (Bildstörungen bei Zustandsstürmen).
5. P4-OTA mit PSRAM-TLS für R15 auf JC8012 V1 und Waveshare 8".
6. Heap-Tracing über lange Laufzeit (Anzahl interner Allokationen < 4 KiB pro Minute)
   vor/nach F5a und R13, um den Fragmentierungseffekt zu belegen.

---

## Anhang: Messbuilds nachstellen

Basis wie `.github/workflows/firmware.yml` (Compile-Schritt, `sketch.yaml` für den
Compile ausgeblendet). Für die Exceptions-Messung zusätzlich:

```sh
--build-property 'compiler.cpp.flags=-MMD -c "@{compiler.sdk.path}/flags/cpp_flags" {compiler.warning_flags} {compiler.optimization_flags} {compiler.common_werror_flags} -fno-exceptions'
```

Für die LVGL-Messung in einer Kopie von `lv_conf.h` auf 0 gesetzt:
`LV_DRAW_SW_SUPPORT_AL88`, `LV_DRAW_SW_SUPPORT_L8`,
`LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED`, `LV_USE_CALENDAR`, `LV_USE_CHECKBOX`,
`LV_USE_IMAGEBUTTON`, `LV_USE_LED`, `LV_USE_LIST`, `LV_USE_MENU`, `LV_USE_MSGBOX`,
`LV_USE_SCALE`, `LV_USE_SPAN`, `LV_USE_TABLE`, `LV_USE_TABVIEW`, `LV_USE_TILEVIEW`,
`LV_USE_WIN`, `LV_USE_ARCLABEL`, `LV_USE_GIF`, `LV_USE_THEME_SIMPLE`,
`LV_USE_THEME_MONO` (`LV_USE_GRID` muss an bleiben).

Symbolliste mit Quellzeilen: `riscv32-esp-elf-nm -S -l -C --size-sort HomeTiles.ino.elf`
(S3: `xtensa-esp-elf-nm`), Adressbereiche der Sektionen aus `readelf -S`.
