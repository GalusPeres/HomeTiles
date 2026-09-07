#include "src/core/i18n/i18n.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace i18n {

static const Strings kStringsDe = {
    "de",
    "de",
    "Sprache:",
    "Zeitzone:",
    "Zeitformat:",
    "Datumsformat:",
    "Auto (Sprache)",
    "Auto (Lokalisierung)",
    "24 Stunden",
    "12 Stunden",

    "Home",
    "Ordner ",

    "Waveshare Admin",
    "Waveshare Admin-Panel",
    "Konfiguration & Übersicht",
    "Klicke auf eine Kachel, um sie zu bearbeiten. Kacheln lassen sich per Drag & Drop verschieben und am Eckgriff vergrößern oder verkleinern. Wähle den Typ und passe die Einstellungen an.",
    "Ordner / Tab löschen",
    "Kachel Einstellungen",
    "Typ",
    "Typ gesperrt: Der Ordner enthält noch Kacheln. Erst leeren – Löschen der Kachel bleibt möglich.",
    "Titel",
    "Kachel-Titel",
    "Icon (MDI)",
    "z.B. home, thermometer, lightbulb",
    "Icon-Liste anzeigen",
    "Farbe",
    "Spalte",
    "Zeile",
    "Breite",
    "Höhe",
    "Änderungen werden automatisch gespeichert.",
    "Kopieren",
    "Einfügen",
    "Löschen",
    "Import / Export (alle Ordner & Kacheln)",
    "Export",
    "Import",
    "Import überschreibt die enthaltenen Ordner/Kacheln und, falls vorhanden, den Screensaver.",
    "WiFi",
    "MQTT",
    "Lokalisierung",
    "Screenshot & Diagnose",
    "Firmware Update",
    "Screenshot erstellen & herunterladen",
    "Speichert /ui_screenshot.jpg auf der microSD-Karte. Die vorhandene Datei wird überschrieben.",
    "Firmware-Datei",
    "Aktuelle Firmware",
    "Update",
    "Hier nur die update.bin hochladen. Die factory.bin ist nur für den ersten Flash gedacht.",
    "Datei auswählen",
    "Keine Datei ausgewählt",

    "WiFi Status",
    "Verbunden",
    "Getrennt",
    "Offline",
    "AP aktiv",
    "WLAN",
    "SSID",
    "IP",
    "Passwort",
    "Statische IP",
    "Gateway",
    "Subnetzmaske",
    "DNS-Server",
    "Leer lassen für DHCP",
    "MQTT nicht konfiguriert",
    "AP aktivieren",
    "AP beenden",
    "Ja",
    "Nein",

    "MQTT Host / IP",
    "Port",
    "Benutzername",
    "Passwort",
    "MQTT Client ID",
    "leer = automatisch",
    "Leer lassen = automatisch aus der MAC-Adresse erzeugen.",
    "Geräte-Topic Basis",
    "Home Assistant Prefix",
    "Uhrzeit Schriftgröße",
    "Datum Schriftgröße",
    "Speichern",
    "Gerät wirklich neu starten?",
    "Neustart",
    "MQTT-Konfiguration gespeichert",
    "Das Gerät verbindet sich neu ...",
    "Bridge-Konfiguration gespeichert",
    "Die Daten wurden per MQTT übertragen.",
    "Speichern fehlgeschlagen",

    "Waveshare WiFi-Konfiguration",
    "WiFi-Konfiguration",
    "Schritt 1: Mit WLAN verbinden",
    "WiFi-Verbindung",
    "SSID (Netzwerkname)",
    "Mein WiFi",
    "Passwort",
    "Passwort",
    "Leer lassen für offenes Netzwerk",
    "Hinweis:",
    "Nach erfolgreicher WLAN-Verbindung kannst du über das Webinterface im normalen Netzwerk die MQTT-Einstellungen konfigurieren.",
    "Speichern & Verbinden",
    "Erfolgreich gespeichert!",
    "Die Konfiguration wurde erfolgreich gespeichert.<br>Das Gerät wird jetzt neu gestartet und versucht sich mit dem WiFi zu verbinden.",
    "Du wirst in 10 Sekunden automatisch weitergeleitet.<br>Falls die Verbindung nicht klappt, aktiviere den Hotspot-Modus erneut über die Einstellungen.",
    "Fehler: WiFi SSID ist erforderlich!",

    "Display",
    "Helligkeit:",
    "Screensaver\nHelligkeit:",
    "Farbton",
    "Sättigung",
    "Sleep:",
    "in",
    "Nie",
    "Screensaver:",
    "Touch",
    "GT911 / kein IMU",

    "Leer",
    "Sensor",
    "Energie",
    "Wetter",
    "Szene",
    "Ordner",
    "Schalter",
    "Media",
    "Uhr",
    "Text",
    "Settings",
    "Zurück",

    "Keine Auswahl",
    "Sensor Entity",
    "Einheit",
    "Nachkommastellen (leer = Originalwert)",
    "Wert-Größe",
    "Anzeige-Modus",
    "Keine",
    "Gauge",
    "Graph",
    "Gauge Min",
    "Gauge Max",
    "Bogengrad (90-359)",
    "Gauge Größe (100-800 px)",
    "Y-Offset (-100 bis 200)",
    "Graph Höhe (20-200 px)",
    "Popup öffnen",
    "Short Press",
    "Long Press",
    "Wert Y-Offset (-100 bis 200)",
    "Weather Entity",
    "Energie-Quelle",
    "Schaltbare Entität / Licht",
    "Anzeige",
    "Icon Button",
    "LVGL Switch",
    "Media Player",
    "Uhrzeit anzeigen",
    "Datum anzeigen",
    "Uhrzeit Schriftgröße",
    "Datum Schriftgröße",
    "Text",
    "Text für die Kachel",
    "Text-Größe",
    "Max 31 Zeichen gespeichert.",
    "Ziel-Ordner",
    "Neuer Ordner",
    "Szene / Skript / Button",

    "Bitte zuerst eine Kachel wählen",
    "Kachel kopiert",
    "Keine kopierte Kachel vorhanden",
    "Kachel eingefügt",
    "Settings-Kachel (fest)",
    "Zurück-Kachel (fest)",
    "Diese Kachel kann nicht gelöscht werden",
    "Dieser Ordner kann nicht gelöscht werden",
    "Ordner \"{name}\" wirklich löschen?\n\nAlle Kacheln in diesem Ordner werden gelöscht und die Ordner-Kachel im übergeordneten Ordner wird entfernt.",
    "Ordner gelöscht",
    "Fehler beim Löschen",
    "Ordner nicht gefunden",
    "Kachel gespeichert & Display aktualisiert!",
    "Unbekannt",
    "Netzwerkfehler",
    "Netzwerkfehler beim Speichern",
    "Export erstellt!",
    "Export fehlgeschlagen",
    "Import-JSON ungültig",
    "Import fehlgeschlagen",
    "Import läuft...",
    "Import abgeschlossen!",
    "Kachel passt dort nicht hin",
    "Keine sinnvolle Anordnung gefunden",
    "Kacheln verschoben & gespeichert!",
    "Fehler beim Verschieben",
    "Nach oben verschieben",
    "Nach unten verschieben",
    "Netzwerkfehler beim Verschieben",
    "Screenshot wird erstellt...",
    "Screenshot gespeichert & Download gestartet!",
    "Screenshot fehlgeschlagen",
    "Bitte zuerst eine update.bin auswählen",
    "Firmware wird aktualisiert...",
    "Update wird installiert...",
    "Warte auf Neustart...",
    "Update erfolgreich installiert. Das Gerät startet jetzt neu.",
    "Firmware-Update fehlgeschlagen",

    // On-device Wi-Fi selection (Settings popup, see tab_settings.cpp).
    "Suche Netzwerke...",
    "Keine Netzwerke gefunden",
    "Neu suchen",
    "Manuell",
    "offen",
    "Passwort für %s",
    "Zurück",
    "Verbinden",
    "Gespeichert – Gerät startet neu...",
    "Speichern fehlgeschlagen",

    "Tastatur:",

    "Helligkeit, Standby & Rotation",
    "Netzwerk & Access Point",
    "Sprache, Zeitzone & Tastatur",
    "%s",

    "Drehen",

    "Gerät",
    "Nach Updates suchen",
    "Suche nach Updates...",
    "Firmware ist aktuell",
    "Update %s verfügbar",
    "2 Neustarts möglich. Danach Version prüfen.",
    "Auf %s aktualisieren",
    "Suche fehlgeschlagen",
    "Update wird geladen...",
    "Update fehlgeschlagen",
    "Installiert! Neustart...",

    "Trennen",
    "Koppeln",
    "Koppeln: MQTT verbindet neu...",

    "Bilder verwenden",
    "Zufällige Reihenfolge",
    "Bilder",
    "Anzeigedauer (Sekunden)",
    "Zoom",
    "Fokus X",
    "Fokus Y",
    "Uhrzeit",
    "Wochentag",
    "Textschatten",
    "Ausrichtung Uhrzeit",
    "Ausrichtung Datum",
    "Links",
    "Zentriert",
    "Rechts",
    "Kachel-Schatten",
    "Kachel-Rahmen",
    "Deckkraft",
    "Hintergrund oder Uhr anklicken. Kacheln in den beiden unteren Reihen lassen sich wie gewohnt verschieben und vergrößern.",
    "microSD erforderlich: Im Stammverzeichnis den Ordner /images erstellen und JPEG-Bilder dort ablegen.",
    "Keine JPEG-Dateien in /images - schwarzer Hintergrund bleibt aktiv.",
    "Screensaver gespeichert!",
    "Screensaver konnte nicht gespeichert werden",
    "Screensaver konnte nicht geladen werden",

    "Ethernet aktivieren",
    "WLAN aktivieren",
    "Netzwerkmodus geändert - gilt nach Neustart",
    "Ethernet statt WLAN (gilt nach Neustart)",

    "DHCP verwenden",
    "DHCP rückgängig",
    "DHCP ausgewählt - gilt nach Neustart",
    "Statische IP ausgewählt - gilt nach Neustart",
    "IP-Konfiguration",
    "DHCP (automatisch)",
    "Statische IP",
    "DHCP ist ausgewählt. Die Adresse wird nach Speichern und Neustart automatisch bezogen.",
    "Erforderlich: Statische IP, Gateway und Subnetzmaske. DNS ist optional. Fehlt eine Angabe oder ist sie ungültig, wird DHCP verwendet. Gilt nach Speichern und Neustart.",
    "Die statische IP-Konfiguration ist unvollständig oder ungültig.",

    "Admin-Panel",
    "Diashow",

    "Netzwerk",
    "Verbindungsart",
    "Die Verbindungsart wird nach dem Neustart verwendet.",
    "Statische IP verwenden",

    "Anzeigen",
    "Verbergen",

    "Crash-Log herunterladen",
    "SD-Diagnose öffnen",
    "Gespeicherter Core-Dump",
    "Core-Dump herunterladen",
    "Core-Dump löschen",
    "Der Core-Dump lässt sich am PC mit esp-coredump und dem Build-ELF zu einem vollständigen Stacktrace auflösen.",

    "Dateimanager",
    "Prüfe...",
    "Aktualisieren",
    "Neuer Ordner",
    "Dateien wählen",
    "Hochladen",
    "Keine Auswahl",
    "Öffnen",
    "Umbenennen",
    "Name",
    "Geändert",
    "Größe",
    "Noch nicht geladen.",

    "Neustart...",

    "An",
    "Aus",
    "Lade...",

    "Wiedergabe",
    "Pausiert",
    "Leerlauf",
    "Standby",
    "Aus",
    "Keine Wiedergabe",

    "Kamera",
    "Kamera",
    "Bereit",
    "Kamerastream wird vorbereitet ...",
    "Bridge wird angefragt ...",
    "Kamera benötigt HomeTiles Bridge v0.6.28 oder neuer",
    "Keine Kamera-Antwort – MQTT oder Bridge nicht erreichbar",
    "Ungültige Kamera-Antwort",
    "Bridge lieferte keine Stream-URL",
    "Stream wird verbunden ...",
    "Stream nicht verfügbar",
    "Stream beendet",
    "Leere Stream-URL",
    "Kamerastream läuft bereits",
    "Kamera-Task konnte nicht starten",
    "Zu wenig PSRAM für Videobild",
    "JPEG-Decoderfehler",
    "Kameraverbindung wird aufgebaut",
    "Stream-URL konnte nicht geöffnet werden",
    "JPEG-Decoder konnte nicht starten",
    "Zu wenig PSRAM für Stream-Puffer",
    "Puffern ...",
    "Streamverbindung beendet",
    "Unerwartete Videoauflösung (%u Bytes)",
    "HTTP-Fehler %d",
    "Kameravideo nur für Waveshare 8 Zoll",
    "Unbekannte Kamera",
    "Kein Kamerabild verfügbar",
    "Home-Assistant-URL nicht verfügbar",
    "Kamerastream konnte nicht vorbereitet werden",
    "JPEG-Eingangspuffer voll",
    "%.1f FPS",
    "MQTT nicht verbunden",
    "Kamera-Topic fehlt",
    "MQTT-Warteschlange voll",

    "I/O",
    "Temperatur",
    "Name",
    "GPIO",
    "Kein freier GPIO",
    "Ausgangslogik",
    "Active high",
    "Active low",
    "High",
    "Low",
    "Nach Neustart",
    "Genauigkeit",
    "0 Nachkommastellen",
    "1 Nachkommastelle",
    "2 Nachkommastellen",
    "3 Nachkommastellen",
    "Zuordnung entfernen",
    "\"{name}\" entfernen?",
    "Keine lokale I/O-Zuordnung. Füge oben einen Schalter oder Temperatursensor hinzu.",
    "Dieses Gerät hat noch kein geprüftes konfigurierbares GPIO-Profil.",
    "Ungespeicherte Änderungen",
    "Kein freier kompatibler GPIO",
    "Name ist erforderlich",
    "Wird gespeichert…",
    "Gespeichert",
    "Laden fehlgeschlagen",
    "I/O-Zuordnungen konnten nicht geladen werden.",
    "Ungespeicherte I/O-Änderungen gehen verloren. Gerät neu starten?",
    "Neustart…",

    "Zugriffsschutz",
    "PIN-Schutz",
    "Kachel ausblenden",
    "PIN",
    "4 bis 8 Ziffern",
    "PIN gespeichert.",
    "Noch keine PIN eingerichtet.",
    "Per Randwisch öffnen",
    "Wischkante",
    "Vom gewählten Rand nach innen wischen. Funktioniert auch bei sichtbarer Kachel.",
    "Links",
    "Rechts",
    "Oben",
    "Unten",
    "Factory-PIN",
    "Der längere Factory-PIN entsperrt Einstellungen und geschützte Ordner. Dies ist eine lokale Kindersicherung, keine Anmeldung.",
    "Mache zuerst ein Feld in Home frei, damit die Einstellungen-Kachel wieder eingefügt werden kann.",
    "Die PIN muss aus 4 bis 8 Ziffern bestehen.",
    "Ordner mit PIN schützen",
        "PIN",
    "4 bis 8 Ziffern",
    "PIN übernehmen",
    "Ordner-PIN gespeichert",
    "Ordner-PIN konnte nicht gespeichert werden",
    "Speichere zuerst die neue Ordner-Kachel.",
    "Einstellungen entsperren",
    "Falsche PIN",
    "%s entsperren",
    "Settings hier ablegen, um die Kachel auszublenden."};

static const Strings kStringsEn = {
    "en",
    "en",
    "Language:",
    "Time zone:",
    "Time format:",
    "Date format:",
    "Auto (language)",
    "Auto (localization)",
    "24-hour",
    "12-hour",

    "Home",
    "Folder ",

    "Waveshare Admin",
    "Waveshare Admin Panel",
    "Configuration & Overview",
    "Click a tile to edit it. Drag and drop tiles to move them, or use the corner handle to resize them. Choose the type and adjust its settings.",
    "Delete Folder / Tab",
    "Tile Settings",
    "Type",
    "Type locked: this folder still contains tiles. Empty it first – deleting the tile is still possible.",
    "Title",
    "Tile title",
    "Icon (MDI)",
    "e.g. home, thermometer, lightbulb",
    "Show icon list",
    "Color",
    "Column",
    "Row",
    "Width",
    "Height",
    "Changes are saved automatically.",
    "Copy",
    "Paste",
    "Delete",
    "Import / Export (all folders & tiles)",
    "Export",
    "Import",
    "Import overwrites the included folders/tiles and, when present, the screensaver.",
    "WiFi",
    "MQTT",
    "Localization",
    "Screenshot & Diagnostics",
    "Firmware Update",
    "Create & Download Screenshot",
    "Saves /ui_screenshot.jpg to the microSD card. The existing file is overwritten.",
    "Firmware file",
    "Current firmware",
    "Update",
    "Upload only the update.bin here. The factory.bin is only for the first flash.",
    "Choose file",
    "No file selected",

    "WiFi Status",
    "Connected",
    "Disconnected",
    "Offline",
    "AP active",
    "WiFi",
    "SSID",
    "IP",
    "Password",
    "Static IP",
    "Gateway",
    "Subnet mask",
    "DNS server",
    "Leave empty for DHCP",
    "MQTT not configured",
    "Enable AP",
    "Disable AP",
    "Yes",
    "No",

    "MQTT Host / IP",
    "Port",
    "Username",
    "Password",
    "MQTT Client ID",
    "empty = automatic",
    "Leave empty to generate it automatically from the MAC address.",
    "Device topic base",
    "Home Assistant prefix",
    "Time font size",
    "Date font size",
    "Save",
    "Restart device now?",
    "Restart",
    "MQTT configuration saved",
    "The device is reconnecting ...",
    "Bridge configuration saved",
    "Data was sent via MQTT.",
    "Save failed",

    "Waveshare WiFi Configuration",
    "WiFi Configuration",
    "Step 1: Connect to WiFi",
    "WiFi Connection",
    "SSID (network name)",
    "My WiFi",
    "Password",
    "Password",
    "Leave empty for an open network",
    "Note:",
    "After the WiFi connection works, you can configure MQTT later through the web interface on your normal network.",
    "Save & Connect",
    "Saved successfully!",
    "The configuration was saved successfully.<br>The device will now restart and try to connect to WiFi.",
    "You will be redirected automatically in 10 seconds.<br>If the connection fails, enable hotspot mode again from the settings.",
    "Error: WiFi SSID is required!",

    "Display",
    "Brightness:",
    "Screensaver\nBrightness:",
    "Hue",
    "Saturation",
    "Sleep:",
    "in",
    "Never",
    "Screensaver:",
    "Touch",
    "GT911 / no IMU",

    "Empty",
    "Sensor",
    "Energy",
    "Weather",
    "Scene",
    "Folder",
    "Switch",
    "Media",
    "Clock",
    "Text",
    "Settings",
    "Back",

    "No selection",
    "Sensor Entity",
    "Unit",
    "Decimals (empty = original value)",
    "Value size",
    "Display mode",
    "None",
    "Gauge",
    "Graph",
    "Gauge Min",
    "Gauge Max",
    "Arc degree (90-359)",
    "Gauge size (100-800 px)",
    "Y offset (-100 to 200)",
    "Graph height (20-200 px)",
    "Open popup",
    "Short Press",
    "Long Press",
    "Value Y offset (-100 to 200)",
    "Weather Entity",
    "Energy Entity",
    "Switchable entity / Light",
    "Display",
    "Icon Button",
    "LVGL Switch",
    "Media Player",
    "Show time",
    "Show date",
    "Time font size",
    "Date font size",
    "Text",
    "Text for the tile",
    "Text size",
    "Max 31 characters are stored.",
    "Target folder",
    "New folder",
    "Scene / Script / Button",

    "Please select a tile first",
    "Tile copied",
    "No copied tile available",
    "Tile pasted",
    "Settings tile (fixed)",
    "Back tile (fixed)",
    "This tile cannot be deleted",
    "This folder cannot be deleted",
    "Delete folder \"{name}\"?\n\nAll tiles in this folder will be deleted and the folder tile in the parent folder will be removed.",
    "Folder deleted",
    "Delete failed",
    "Folder not found",
    "Tile saved & display updated!",
    "Unknown",
    "Network error",
    "Network error while saving",
    "Export created!",
    "Export failed",
    "Invalid import JSON",
    "Import failed",
    "Import in progress...",
    "Import complete!",
    "Tile does not fit there",
    "No valid arrangement found",
    "Tiles moved & saved!",
    "Move failed",
    "Move up",
    "Move down",
    "Network error while moving",
    "Creating screenshot...",
    "Screenshot saved & download started!",
    "Screenshot failed",
    "Please select an update.bin first",
    "Updating firmware...",
    "Installing update...",
    "Waiting for restart...",
    "Update installed successfully. The device is restarting now.",
    "Firmware update failed",

    // On-device WiFi selection (Settings popup, see tab_settings.cpp)
    "Scanning for networks...",
    "No networks found",
    "Scan again",
    "Manual",
    "open",
    "Password for %s",
    "Back",
    "Connect",
    "Saved - device is restarting...",
    "Saving failed",

    "Keyboard:",

    "Brightness, standby & rotation",
    "Network & access point",
    "Language, time zone & keyboard",
    "%s",

    "Rotate",

    "Device",
    "Check for updates",
    "Checking for updates...",
    "Firmware is up to date",
    "Update %s available",
    "May restart twice. Check version afterwards.",
    "Update to %s",
    "Check failed",
    "Downloading update...",
    "Update failed",
    "Installed! Restarting...",

    "Disconnect",
    "Pairing",
    "Pairing: reconnecting MQTT...",

    "Use images",
    "Shuffle",
    "Images",
    "Duration (seconds)",
    "Zoom",
    "Focus X",
    "Focus Y",
    "Clock",
    "Weekday",
    "Text shadow",
    "Time alignment",
    "Date alignment",
    "Left",
    "Centered",
    "Right",
    "Tile shadows",
    "Tile borders",
    "Opacity",
    "Click the background or clock. Tiles in the bottom two rows can be moved and resized as usual.",
    "microSD required: Create /images in the card root and place JPEG images there.",
    "No JPEG files in /images - the black background remains active.",
    "Screensaver saved!",
    "Could not save screensaver",
    "Could not load screensaver",

    "Enable Ethernet",
    "Enable WiFi",
    "Network mode changed - applies after restart",
    "Ethernet instead of WiFi (applies after restart)",

    "Use DHCP",
    "Undo DHCP",
    "DHCP selected - applies after restart",
    "Static IP selected - applies after restart",
    "IP configuration",
    "DHCP (automatic)",
    "Static IP",
    "DHCP is selected. The address is assigned automatically after Save and Restart.",
    "Required: Static IP, gateway and subnet mask. DNS is optional. If a value is missing or invalid, DHCP is used. Applies after Save and Restart.",
    "The static IP configuration is incomplete or invalid.",

    "Admin Panel",
    "Slideshow",

    "Network",
    "Connection type",
    "The connection type is used after restart.",
    "Use static IP address",

    "Show",
    "Hide",

    "Download crash log",
    "Open SD diagnostics",
    "Stored core dump",
    "Download core dump",
    "Delete core dump",
    "Decode the core dump on a PC with esp-coredump and the build ELF to get a full stack trace.",

    "File Manager",
    "Checking...",
    "Refresh",
    "New folder",
    "Choose files",
    "Upload",
    "No selection",
    "Open",
    "Rename",
    "Name",
    "Modified",
    "Size",
    "Not loaded yet.",

    "Restarting...",

    "On",
    "Off",
    "Loading...",

    "Playing",
    "Paused",
    "Idle",
    "Standby",
    "Off",
    "Nothing playing",

    "Camera",
    "Camera",
    "Ready",
    "Preparing camera stream ...",
    "Requesting bridge ...",
    "Camera requires HomeTiles Bridge v0.6.28 or newer",
    "No camera response – MQTT or bridge unavailable",
    "Invalid camera response",
    "Bridge did not provide a stream URL",
    "Connecting stream ...",
    "Stream unavailable",
    "Stream stopped",
    "Empty stream URL",
    "Camera stream is already running",
    "Camera task could not start",
    "Not enough PSRAM for the video frame",
    "JPEG decoder error",
    "Connecting to camera",
    "Stream URL could not be opened",
    "JPEG decoder could not start",
    "Not enough PSRAM for the stream buffer",
    "Buffering ...",
    "Stream connection ended",
    "Unexpected video resolution (%u bytes)",
    "HTTP error %d",
    "Camera video is only available on Waveshare 8 inch",
    "Unknown camera",
    "Camera image unavailable",
    "Home Assistant URL unavailable",
    "Camera stream setup failed",
    "JPEG input buffer full",
    "%.1f FPS",
    "MQTT disconnected",
    "Camera topic missing",
    "MQTT queue full",

    "I/O",
    "Temperature",
    "Name",
    "GPIO",
    "No free GPIO",
    "Output logic",
    "Active high",
    "Active low",
    "High",
    "Low",
    "After restart",
    "Precision",
    "0 decimals",
    "1 decimal",
    "2 decimals",
    "3 decimals",
    "Remove assignment",
    "Remove \"{name}\"?",
    "No local I/O assigned. Add a switch or temperature sensor above.",
    "This device has no verified configurable GPIO profile yet.",
    "Unsaved changes",
    "No free compatible GPIO",
    "Name is required",
    "Saving…",
    "Saved",
    "Load failed",
    "Could not load I/O assignments.",
    "Unsaved I/O changes will be lost. Restart device?",
    "Restarting…",

    "Access control",
    "PIN protection",
    "Hide tile",
    "PIN",
    "4 to 8 digits",
    "PIN saved.",
    "No PIN is configured yet.",
    "Open by edge swipe",
    "Swipe edge",
    "Swipe inward from the selected edge. This also works while the tile is visible.",
    "Left",
    "Right",
    "Top",
    "Bottom",
    "Factory PIN",
    "The longer Factory PIN unlocks Settings and protected folders. This is a local child lock, not a login.",
    "Free one cell in Home first so the Settings tile can be restored.",
    "The PIN must contain 4 to 8 digits.",
    "Protect this folder with a PIN",
        "PIN",
    "4 to 8 digits",
    "Apply PIN",
    "Folder PIN saved",
    "Folder PIN could not be saved",
    "Save the new folder tile first.",
    "Unlock Settings",
    "Incorrect PIN",
    "Unlock %s",
    "Drop Settings here to hide the tile."};

static const Strings kStringsFr = {
    "fr",
    "fr",
    "Langue :",
    "Fuseau horaire :",
    "Format de l'heure :",
    "Format de date :",
    "Auto (langue)",
    "Auto (localisation)",
    "24 heures",
    "12 heures",

    "Accueil",
    "Dossier ",

    "Waveshare Admin",
    "Panneau d'admin Waveshare",
    "Configuration & aperçu",
    "Clique sur une tuile pour la modifier. Les tuiles se déplacent par glisser-déposer et se redimensionnent par la poignée d'angle. Choisis le type (capteur/météo/scène/touche/dossier/réglages/interrupteur/média/image/horloge/texte) et ajuste les réglages.",
    "Supprimer le dossier / l'onglet",
    "Réglages de la tuile",
    "Type",
    "Type verrouillé : le dossier contient encore des tuiles. Vide-le d'abord – la suppression de la tuile reste possible.",
    "Titre",
    "Titre de la tuile",
    "Icône (MDI)",
    "p. ex. home, thermometer, lightbulb",
    "Afficher la liste des icônes",
    "Couleur",
    "Colonne",
    "Ligne",
    "Largeur",
    "Hauteur",
    "Les modifications sont enregistrées automatiquement.",
    "Copier",
    "Coller",
    "Supprimer",
    "Import / export (tous les dossiers & tuiles)",
    "Export",
    "Import",
    "L'import remplace les dossiers/tuiles contenus et, le cas échéant, l'écran de veille.",
    "WiFi",
    "MQTT",
    "Localisation",
    "Capture d'écran & diagnostic",
    "Mise à jour du firmware",
    "Créer & télécharger une capture d'écran",
    "Enregistre /ui_screenshot.jpg sur la carte microSD. Le fichier existant est remplacé.",
    "Fichier du firmware",
    "Firmware actuel",
    "Mise à jour",
    "Téléverser uniquement le fichier update.bin ici. Le fichier factory.bin ne sert qu'au premier flash.",
    "Choisir un fichier",
    "Aucun fichier sélectionné",

    "État WiFi",
    "Connecté",
    "Déconnecté",
    "Hors ligne",
    "AP actif",
    "WiFi",
    "SSID",
    "IP",
    "Mot de passe",
    "IP statique",
    "Passerelle",
    "Masque de sous-réseau",
    "Serveur DNS",
    "Laisser vide pour DHCP",
    "MQTT non configuré",
    "Activer l'AP",
    "Arrêter l'AP",
    "Oui",
    "Non",

    "Hôte MQTT / IP",
    "Port",
    "Nom d'utilisateur",
    "Mot de passe",
    "MQTT Client ID",
    "vide = automatique",
    "Laisser vide = générer automatiquement depuis l'adresse MAC.",
    "Topic de base de l'appareil",
    "Préfixe Home Assistant",
    "Taille de police de l'heure",
    "Taille de police de la date",
    "Enregistrer",
    "Vraiment redémarrer l'appareil ?",
    "Redémarrer",
    "Configuration MQTT enregistrée",
    "L'appareil se reconnecte ...",
    "Configuration du bridge enregistrée",
    "Les données ont été transmises via MQTT.",
    "Échec de l'enregistrement",

    "Configuration WiFi Waveshare",
    "Configuration WiFi",
    "Étape 1 : se connecter au WiFi",
    "Connexion WiFi",
    "SSID (nom du réseau)",
    "Mon WiFi",
    "Mot de passe",
    "Mot de passe",
    "Laisser vide pour un réseau ouvert",
    "Remarque :",
    "Une fois la connexion WiFi établie, tu peux configurer les réglages MQTT via l'interface web sur le réseau normal.",
    "Enregistrer & connecter",
    "Enregistré avec succès !",
    "La configuration a été enregistrée.<br>L'appareil redémarre maintenant et essaie de se connecter au WiFi.",
    "Tu seras redirigé automatiquement dans 10 secondes.<br>Si la connexion échoue, réactive le mode hotspot depuis les réglages.",
    "Erreur : le SSID WiFi est requis !",

    "Écran",
    "Luminosité :",
    "Écran de veille\nLuminosité :",
    "Teinte",
    "Saturation",
    "Veille :",
    "dans",
    "Jamais",
    "Écran de veille :",
    "Tactile",
    "GT911 / pas d'IMU",

    "Vide",
    "Capteur",
    "Énergie",
    "Météo",
    "Scène",
    "Dossier",
    "Interrupteur",
    "Média",
    "Horloge",
    "Texte",
    "Réglages",
    "Retour",

    "Aucune sélection",
    "Entité capteur",
    "Unité",
    "Décimales (vide = valeur brute)",
    "Taille de la valeur",
    "Mode d'affichage",
    "Aucun",
    "Jauge",
    "Graphique",
    "Jauge min",
    "Jauge max",
    "Degré d'arc (90-359)",
    "Taille de la jauge (100-800 px)",
    "Décalage Y (-100 à 200)",
    "Hauteur du graphique (20-200 px)",
    "Ouvrir le popup",
    "Appui court",
    "Appui long",
    "Décalage Y de la valeur (-100 à 200)",
    "Entité météo",
    "Source d'énergie",
    "Entité commutable / Lumière",
    "Affichage",
    "Bouton icône",
    "Interrupteur LVGL",
    "Lecteur multimédia",
    "Afficher l'heure",
    "Afficher la date",
    "Taille de police de l'heure",
    "Taille de police de la date",
    "Texte",
    "Texte de la tuile",
    "Taille du texte",
    "31 caractères max enregistrés.",
    "Dossier cible",
    "Nouveau dossier",
    "Scène / Script / Bouton",

    "Choisis d'abord une tuile",
    "Tuile copiée",
    "Aucune tuile copiée",
    "Tuile collée",
    "Tuile Réglages (fixe)",
    "Tuile Retour (fixe)",
    "Cette tuile ne peut pas être supprimée",
    "Ce dossier ne peut pas être supprimé",
    "Vraiment supprimer le dossier \"{name}\" ?\n\nToutes les tuiles de ce dossier seront supprimées et la tuile du dossier sera retirée du dossier parent.",
    "Dossier supprimé",
    "Erreur lors de la suppression",
    "Dossier introuvable",
    "Tuile enregistrée & écran actualisé !",
    "Inconnu",
    "Erreur réseau",
    "Erreur réseau lors de l'enregistrement",
    "Export créé !",
    "Échec de l'export",
    "JSON d'import invalide",
    "Échec de l'import",
    "Import en cours...",
    "Import terminé !",
    "La tuile ne rentre pas à cet endroit",
    "Aucune disposition valable trouvée",
    "Tuiles déplacées & enregistrées !",
    "Erreur lors du déplacement",
    "Déplacer vers le haut",
    "Déplacer vers le bas",
    "Erreur réseau lors du déplacement",
    "Création de la capture d'écran...",
    "Capture enregistrée & téléchargement lancé !",
    "Échec de la capture d'écran",
    "Choisis d'abord un fichier update.bin",
    "Mise à jour du firmware...",
    "Installation de la mise à jour...",
    "En attente du redémarrage...",
    "Mise à jour installée. L'appareil redémarre maintenant.",
    "Échec de la mise à jour du firmware",

    // On-device Wi-Fi selection (Settings popup, see tab_settings.cpp).
    "Recherche de réseaux...",
    "Aucun réseau trouvé",
    "Rechercher à nouveau",
    "Manuel",
    "ouvert",
    "Mot de passe pour %s",
    "Retour",
    "Se connecter",
    "Enregistré – l'appareil redémarre...",
    "Échec de l'enregistrement",

    "Clavier :",

    "Luminosité, veille & rotation",
    "Réseau & point d'accès",
    "Langue, fuseau horaire & clavier",
    "%s",

    "Pivoter",

    "Appareil",
    "Rechercher des mises à jour",
    "Recherche de mises à jour...",
    "Le firmware est à jour",
    "Mise à jour %s disponible",
    "2 redémarrages possibles. Vérifie ensuite la version.",
    "Mettre à jour vers %s",
    "Échec de la recherche",
    "Téléchargement de la mise à jour...",
    "Échec de la mise à jour",
    "Installé ! Redémarrage...",

    "Déconnecter",
    "Appairer",
    "Appairage : MQTT se reconnecte...",

    "Utiliser des images",
    "Ordre aléatoire",
    "Images",
    "Durée d'affichage (secondes)",
    "Zoom",
    "Focus X",
    "Focus Y",
    "Heure",
    "Jour de la semaine",
    "Ombre du texte",
    "Alignement de l'heure",
    "Alignement de la date",
    "Gauche",
    "Centré",
    "Droite",
    "Ombre des tuiles",
    "Bordure des tuiles",
    "Opacité",
    "Clique sur l'arrière-plan ou l'horloge. Les tuiles des deux rangées du bas se déplacent et s'agrandissent comme d'habitude.",
    "microSD requise : crée le dossier /images à la racine et places-y des images JPEG.",
    "Aucun fichier JPEG dans /images - l'arrière-plan noir reste actif.",
    "Écran de veille enregistré !",
    "L'écran de veille n'a pas pu être enregistré",
    "L'écran de veille n'a pas pu être chargé",

    "Activer l'Ethernet",
    "Activer le WiFi",
    "Mode réseau modifié - appliqué après redémarrage",
    "Ethernet au lieu du WiFi (appliqué après redémarrage)",

    "Utiliser DHCP",
    "Annuler DHCP",
    "DHCP sélectionné - appliqué après redémarrage",
    "IP statique sélectionnée - appliquée après redémarrage",
    "Configuration IP",
    "DHCP (automatique)",
    "IP statique",
    "DHCP est sélectionné. L'adresse sera attribuée automatiquement après enregistrement et redémarrage.",
    "Requis : IP statique, passerelle et masque de sous-réseau. Le DNS est facultatif. Si une valeur manque ou est invalide, DHCP est utilisé. Appliqué après enregistrement et redémarrage.",
    "La configuration IP statique est incomplète ou invalide.",

    "Panneau d'admin",
    "Diaporama",

    "Réseau",
    "Type de connexion",
    "Le type de connexion est utilisé après le redémarrage.",
    "Utiliser une IP statique",

    "Afficher",
    "Masquer",

    "Télécharger le journal de crash",
    "Ouvrir le diagnostic SD",
    "Core dump enregistré",
    "Télécharger le core dump",
    "Supprimer le core dump",
    "Le core dump peut être résolu sur PC avec esp-coredump et l'ELF du build pour obtenir une stack trace complète.",

    "Gestionnaire de fichiers",
    "Vérification...",
    "Actualiser",
    "Nouveau dossier",
    "Choisir des fichiers",
    "Téléverser",
    "Aucune sélection",
    "Ouvrir",
    "Renommer",
    "Nom",
    "Modifié",
    "Taille",
    "Pas encore chargé.",

    "Redémarrage...",

    "Allumé",
    "Éteint",
    "Chargement...",

    "Lecture",
    "En pause",
    "Inactif",
    "Veille",
    "Éteint",
    "Aucune lecture",

    "Caméra",
    "Caméra",
    "Prêt",
    "Préparation du flux caméra ...",
    "Interrogation du bridge ...",
    "La caméra nécessite HomeTiles Bridge v0.6.28 ou une version plus récente",
    "Aucune réponse caméra – MQTT ou bridge indisponible",
    "Réponse caméra invalide",
    "Le bridge n'a fourni aucune URL de flux",
    "Connexion au flux ...",
    "Flux indisponible",
    "Flux arrêté",
    "URL de flux vide",
    "Le flux caméra est déjà actif",
    "Impossible de démarrer la tâche caméra",
    "PSRAM insuffisante pour l'image vidéo",
    "Erreur du décodeur JPEG",
    "Connexion à la caméra",
    "Impossible d'ouvrir l'URL du flux",
    "Impossible de démarrer le décodeur JPEG",
    "PSRAM insuffisante pour le tampon du flux",
    "Mise en mémoire tampon ...",
    "Connexion au flux terminée",
    "Résolution vidéo inattendue (%u octets)",
    "Erreur HTTP %d",
    "Vidéo de caméra disponible uniquement sur Waveshare 8 pouces",
    "Caméra inconnue",
    "Image de caméra indisponible",
    "URL Home Assistant indisponible",
    "Impossible de préparer le flux caméra",
    "Tampon d'entrée JPEG plein",
    "%.1f FPS",
    "MQTT déconnecté",
    "Topic caméra manquant",
    "File d'attente MQTT pleine",

    "I/O",
    "Température",
    "Nom",
    "GPIO",
    "Aucun GPIO libre",
    "Logique de sortie",
    "Active high",
    "Active low",
    "High",
    "Low",
    "Après redémarrage",
    "Précision",
    "0 décimale",
    "1 décimale",
    "2 décimales",
    "3 décimales",
    "Supprimer l’affectation",
    "Supprimer « {name} » ?",
    "Aucune E/S locale n’est affectée. Ajoutez un interrupteur ou un capteur de température ci-dessus.",
    "Cet appareil n’a pas encore de profil GPIO configurable vérifié.",
    "Modifications non enregistrées",
    "Aucun GPIO compatible libre",
    "Le nom est obligatoire",
    "Enregistrement…",
    "Enregistré",
    "Échec du chargement",
    "Impossible de charger les affectations d’E/S.",
    "Les modifications d’E/S non enregistrées seront perdues. Redémarrer l’appareil ?",
    "Redémarrage…",

    "Contrôle d’accès",
    "Protection PIN",
    "Masquer la tuile",
    "Code PIN",
    "4 à 8 chiffres",
    "Code PIN enregistré.",
    "Aucun code PIN n’est encore configuré.",
    "Ouvrir par balayage",
    "Bord de balayage",
    "Balayez vers l’intérieur depuis le bord choisi. Cela fonctionne aussi lorsque la tuile est visible.",
    "Gauche",
    "Droite",
    "Haut",
    "Bas",
    "Code PIN d’usine",
    "Le code PIN d’usine plus long déverrouille les réglages et les dossiers protégés. Il s’agit d’un contrôle parental local, pas d’une connexion.",
    "Libérez d’abord une case dans Accueil afin de restaurer la tuile Réglages.",
    "Le code PIN doit contenir entre 4 et 8 chiffres.",
    "Protéger ce dossier par un code PIN",
        "Code PIN",
    "4 à 8 chiffres",
    "Appliquer le code PIN",
    "Code PIN du dossier enregistré",
    "Impossible d’enregistrer le code PIN du dossier",
    "Enregistrez d’abord la nouvelle tuile de dossier.",
    "Déverrouiller les réglages",
    "Code PIN incorrect",
    "Déverrouiller %s",
    "Déposez Réglages ici pour masquer la tuile."};

static const LocaleProfile kLocaleDe = {
    "de",
    "Deutsch",
    ",",
    "Heute",
    "Morgen",
    {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"},
    {"Jan.", "Feb.", "Mär.", "Apr.", "Mai", "Jun.",
     "Jul.", "Aug.", "Sep.", "Okt.", "Nov.", "Dez."},
    {"Klare Nacht", "Bewölkt", "Ausnahme", "Nebel", "Hagel",
     "Gewitter", "Gewitterregen", "Teilw. bewölkt", "Starkregen",
     "Regen", "Schnee", "Schneeregen", "Sonnig", "Windig", "Böig"},
    "Klima",
    "Klima-Entity",
    "Solltemperatur",
    "Soll-Luftfeuchtigkeit",
    "Heiz-Sollwert",
    "Kühl-Sollwert",
    {"Heizbetrieb", "Vorheizen", "Kühlbetrieb", "Entfeuchtung", "Lüfter",
     "Abtauen", "Leerlauf", "Aus", "Heizen", "Kühlen", "Heizen/Kühlen",
     "Auto", "Entfeuchten", "Lüfter", "Klima", "Nicht verfügbar",
     "Unbekannt"},
    {"Aktuell", "Soll-Temperatur", "Aktuelle Luftfeuchtigkeit"},
    {"Modus", "Voreinstellung", "Lüftermodus", "Oszillationsart",
     "Horizontale Oszillationsart"},
    {"Ohne", "Eco", "Abwesend", "Boost", "Komfort", "Zuhause", "Schlafen",
     "Aktivität", "Auto", "Niedrig", "Mittel", "Hoch", "An", "Aus",
     "Oben", "Mitte", "Fokus", "Verteilt", "Vertikal", "Horizontal",
     "Beide", "Links", "Mitte", "Rechts", "Schwenken", "Breit"},
    {"Automatisch", "Leer", "Leer / entfernen", "Leeres Feld",
     "Inhalt des ausgewählten Feldes", "Waagerecht", "Senkrecht"},
    {"Cover", "Cover-Entität", "Position", "Lamellen",
     "Öffnen", "Stopp", "Schließen"},
    {"Offen", "Öffnet", "Geschlossen", "Schließt",
     "Nicht verfügbar", "Unbekannt"},
    1,  // 24-hour
    1,  // day.month.year
    " Uhr",
    "{d}. {m}",
    {"Sonntag", "Montag", "Dienstag", "Mittwoch",
     "Donnerstag", "Freitag", "Samstag"},
    {"Global", "Europa", "Amerika", "Afrika & Naher Osten", "Asien",
     "Ozeanien"},
    {"UTC+0 - UTC",
     "UTC+0 / UTC+1 - London",
     "UTC+1 / UTC+2 - Berlin",
     "UTC+2 / UTC+3 - Athen",
     "UTC+3 - Istanbul",
     "UTC+3 - Moskau",
     "UTC-10 - Honolulu",
     "UTC-8 / UTC-7 - Los Angeles",
     "UTC-7 - Phoenix",
     "UTC-7 / UTC-6 - Denver",
     "UTC-6 / UTC-5 - Chicago",
     "UTC-5 / UTC-4 - New York",
     "UTC-3 - Buenos Aires",
     "UTC-3 - Sao Paulo",
     "UTC+2 - Johannesburg",
     "UTC+3 - Nairobi",
     "UTC+4 - Dubai",
     "UTC+5 - Karatschi",
     "UTC+5:30 - Kolkata",
     "UTC+6 - Dhaka",
     "UTC+7 - Bangkok",
     "UTC+8 - Singapur",
     "UTC+8 - Perth",
     "UTC+9 - Tokio",
     "UTC+9:30 - Darwin",
     "UTC+10 / UTC+11 - Sydney",
     "UTC+12 / UTC+13 - Auckland"},
    {"Binärsensor", "Binärsensor-Entity", "Verlauf", "Aktivität",
     "Verlauf nicht verfügbar", "Keine Aktivität", "24H", "7D"},
    {"An", "Aus", "Niedrig", "Normal", "Lädt", "Lädt nicht",
     "Erkannt", "Frei", "Kalt", "Verbunden", "Getrennt", "Offen",
     "Geschlossen", "Heiß", "Entriegelt", "Verriegelt", "Nass",
     "Trocken", "In Bewegung", "Gestoppt", "Eingesteckt", "Ausgesteckt",
     "Strom erkannt", "Kein Strom", "Zuhause", "Abwesend", "Problem",
     "OK", "Läuft", "Läuft nicht", "Unsicher", "Sicher",
     "Update verfügbar", "Aktuell", "Nicht verfügbar", "Unbekannt"},
    {"Zahl", "Auswahl", "Datum/Uhrzeit", "Zahlen-Entity", "Auswahl-Entity", "Datum-/Uhrzeit-Entity", "Übernehmen", "Ungültiger Wert", "Wird übernommen…", "Änderung fehlgeschlagen", "JJJJ-MM-TT", "HH:MM:SS", "JJJJ-MM-TT HH:MM:SS", "Jahr", "Monat", "Tag", "hh", "mm", "ss"}};

static const LocaleProfile kLocaleEn = {
    "en",
    "English",
    ".",
    "Today",
    "Tomorrow",
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"},
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
    {"Clear night", "Cloudy", "Exceptional", "Fog", "Hail",
     "Lightning", "Lightning rain", "Partly cloudy", "Pouring",
     "Rain", "Snow", "Sleet", "Sunny", "Windy", "Windy"},
    "Climate",
    "Climate Entity",
    "Target",
    "Target humidity",
    "Heating target",
    "Cooling target",
    {"Heating", "Preheating", "Cooling", "Drying", "Fan", "Defrosting",
     "Idle", "Off", "Heat", "Cool", "Heat/Cool", "Auto", "Dry",
     "Fan only", "Climate", "Unavailable", "Unknown"},
    {"Current", "Target temperature", "Current humidity"},
    {"Mode", "Preset", "Fan mode", "Swing mode",
     "Horizontal swing mode"},
    {"None", "Eco", "Away", "Boost", "Comfort", "Home", "Sleep",
     "Activity", "Auto", "Low", "Medium", "High", "On", "Off",
     "Top", "Middle", "Focus", "Diffuse", "Vertical", "Horizontal",
     "Both", "Left", "Center", "Right", "Swing", "Wide"},
    {"Automatic", "Empty", "Empty / remove", "Empty field",
     "Selected field content", "Horizontal", "Vertical"},
    {"Cover", "Cover entity", "Position", "Tilt",
     "Open", "Stop", "Close"},
    {"Open", "Opening", "Closed", "Closing", "Unavailable", "Unknown"},
    2,  // 12-hour
    2,  // month/day/year
    ":00",
    "{m} {d}",
    {"Sunday", "Monday", "Tuesday", "Wednesday",
     "Thursday", "Friday", "Saturday"},
    {"Global", "Europe", "Americas", "Africa & Middle East", "Asia",
     "Oceania"},
    {"UTC+0 - UTC",
     "UTC+0 / UTC+1 - London",
     "UTC+1 / UTC+2 - Berlin",
     "UTC+2 / UTC+3 - Athens",
     "UTC+3 - Istanbul",
     "UTC+3 - Moscow",
     "UTC-10 - Honolulu",
     "UTC-8 / UTC-7 - Los Angeles",
     "UTC-7 - Phoenix",
     "UTC-7 / UTC-6 - Denver",
     "UTC-6 / UTC-5 - Chicago",
     "UTC-5 / UTC-4 - New York",
     "UTC-3 - Buenos Aires",
     "UTC-3 - Sao Paulo",
     "UTC+2 - Johannesburg",
     "UTC+3 - Nairobi",
     "UTC+4 - Dubai",
     "UTC+5 - Karachi",
     "UTC+5:30 - Kolkata",
     "UTC+6 - Dhaka",
     "UTC+7 - Bangkok",
     "UTC+8 - Singapore",
     "UTC+8 - Perth",
     "UTC+9 - Tokyo",
     "UTC+9:30 - Darwin",
     "UTC+10 / UTC+11 - Sydney",
     "UTC+12 / UTC+13 - Auckland"},
    {"Binary sensor", "Binary sensor entity", "History", "Activity",
     "History unavailable", "No activity", "24H", "7D"},
    {"On", "Off", "Low", "Normal", "Charging", "Not charging",
     "Detected", "Clear", "Cold", "Connected", "Disconnected", "Open",
     "Closed", "Hot", "Unlocked", "Locked", "Wet", "Dry", "Moving",
     "Stopped", "Plugged in", "Unplugged", "Power detected", "No power",
     "Home", "Away", "Problem", "OK", "Running", "Not running",
     "Unsafe", "Safe", "Update available", "Up to date", "Unavailable",
     "Unknown"},
    {"Number", "Select", "Date/time", "Number entity", "Select entity", "Date/time entity", "Apply", "Invalid value", "Applying…", "Change failed", "YYYY-MM-DD", "HH:MM:SS", "YYYY-MM-DD HH:MM:SS", "Year", "Month", "Day", "hh", "mm", "ss"}};

static const LocaleProfile kLocaleFr = {
    "fr",
    "Français",
    ",",
    "Aujourd'hui",
    "Demain",
    {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"},
    {"janv.", "févr.", "mars", "avr.", "mai", "juin",
     "juil.", "août", "sept.", "oct.", "nov.", "déc."},
    {"Nuit claire", "Nuageux", "Exceptionnel", "Brouillard", "Grêle",
     "Orage", "Orage pluvieux", "Partiellement nuageux", "Pluie forte",
     "Pluie", "Neige", "Neige fondue", "Ensoleillé", "Venteux", "Rafales"},
    "Climat",
    "Entité climat",
    "Température cible",
    "Humidité cible",
    "Consigne de chauffage",
    "Consigne de refroidissement",
    {"Chauffage", "Préchauffage", "Refroidissement", "Séchage", "Ventilation",
     "Dégivrage", "Inactif", "Éteint", "Chauffer", "Refroidir",
     "Chauffer/Refroidir", "Auto", "Déshumidifier", "Ventilateur seul",
     "Climat", "Indisponible", "Inconnu"},
    {"Actuel", "Température cible", "Humidité actuelle"},
    {"Mode", "Préréglage", "Mode ventilation", "Mode d'oscillation",
     "Mode d'oscillation horizontale"},
    {"Aucun", "Éco", "Absent", "Boost", "Confort", "Maison", "Sommeil",
     "Activité", "Auto", "Bas", "Moyen", "Élevé", "Allumé", "Éteint",
     "Haut", "Milieu", "Focalisé", "Diffus", "Vertical", "Horizontal",
     "Les deux", "Gauche", "Centre", "Droite", "Oscillation", "Large"},
    {"Automatique", "Vide", "Vide / retirer", "Champ vide",
     "Contenu du champ sélectionné", "Horizontal", "Vertical"},
    {"Volet", "Entité du volet", "Position", "Inclinaison",
     "Ouvrir", "Arrêter", "Fermer"},
    {"Ouvert", "Ouverture", "Fermé", "Fermeture",
     "Indisponible", "Inconnu"},
    1,  // 24-hour
    1,  // day/month/year
    " h",
    "{d} {m}",
    {"Dimanche", "Lundi", "Mardi", "Mercredi",
     "Jeudi", "Vendredi", "Samedi"},
    {"Global", "Europe", "Amériques", "Afrique & Moyen-Orient", "Asie",
     "Océanie"},
    {"UTC+0 - UTC",
     "UTC+0 / UTC+1 - Londres",
     "UTC+1 / UTC+2 - Berlin",
     "UTC+2 / UTC+3 - Athènes",
     "UTC+3 - Istanbul",
     "UTC+3 - Moscou",
     "UTC-10 - Honolulu",
     "UTC-8 / UTC-7 - Los Angeles",
     "UTC-7 - Phoenix",
     "UTC-7 / UTC-6 - Denver",
     "UTC-6 / UTC-5 - Chicago",
     "UTC-5 / UTC-4 - New York",
     "UTC-3 - Buenos Aires",
     "UTC-3 - Sao Paulo",
     "UTC+2 - Johannesburg",
     "UTC+3 - Nairobi",
     "UTC+4 - Dubaï",
     "UTC+5 - Karachi",
     "UTC+5:30 - Calcutta",
     "UTC+6 - Dacca",
     "UTC+7 - Bangkok",
     "UTC+8 - Singapour",
     "UTC+8 - Perth",
     "UTC+9 - Tokyo",
     "UTC+9:30 - Darwin",
     "UTC+10 / UTC+11 - Sydney",
     "UTC+12 / UTC+13 - Auckland"},
    {"Capteur binaire", "Entité du capteur binaire", "Historique",
     "Activité", "Historique indisponible", "Aucune activité", "24H", "7D"},
    {"Activé", "Désactivé", "Faible", "Normal", "En charge",
     "Pas en charge", "Détecté", "Aucune détection", "Froid", "Connecté",
     "Déconnecté", "Ouvert", "Fermé", "Chaud", "Déverrouillé",
     "Verrouillé", "Humide", "Sec", "En mouvement", "Arrêté", "Branché",
     "Débranché", "Alimentation détectée", "Aucune alimentation", "Présent",
     "Absent", "Problème", "OK", "En cours", "À l’arrêt", "Dangereux",
     "Sûr", "Mise à jour disponible", "À jour", "Indisponible", "Inconnu"},
    {"Nombre", "Sélection", "Date/heure", "Entité numérique", "Entité de sélection", "Entité de date/heure", "Appliquer", "Valeur invalide", "Application…", "Échec de la modification", "AAAA-MM-JJ", "HH:MM:SS", "AAAA-MM-JJ HH:MM:SS", "Année", "Mois", "Jour", "hh", "mm", "ss"}};

// Codes and groups match LocaleProfile::timezone_labels and
// timezone_group_labels (see i18n.h).
static const TimezoneOptionInfo kTimezoneOptions[kTimezoneOptionCount] = {
    {0, "utc"},
    {1, "london"},
    {1, "berlin"},
    {1, "athens"},
    {1, "istanbul"},
    {1, "moscow"},
    {2, "honolulu"},
    {2, "los_angeles"},
    {2, "phoenix"},
    {2, "denver"},
    {2, "chicago"},
    {2, "new_york"},
    {2, "buenos_aires"},
    {2, "sao_paulo"},
    {3, "johannesburg"},
    {3, "nairobi"},
    {3, "dubai"},
    {4, "karachi"},
    {4, "kolkata"},
    {4, "dhaka"},
    {4, "bangkok"},
    {4, "singapore"},
    {4, "perth"},
    {4, "tokyo"},
    {5, "darwin"},
    {5, "sydney"},
    {5, "auckland"},
};

const TimezoneOptionInfo& timezone_option(size_t index) {
  if (index >= kTimezoneOptionCount) index = 0;
  return kTimezoneOptions[index];
}

struct LanguageEntry {
  const Strings* strings;
  const LocaleProfile* locale;
};

static const LanguageEntry kLanguages[] = {
    {&kStringsEn, &kLocaleEn},
    {&kStringsDe, &kLocaleDe},
    // The French strings stay compiled and maintained, but are intentionally
    // hidden until the language pass is complete. Define this flag to test it.
#if defined(HOMETILES_ENABLE_FRENCH)
    {&kStringsFr, &kLocaleFr},
#endif
};

static size_t find_language_index(const char* language_code) {
  String code = language_code ? String(language_code) : String();
  code.trim();
  for (size_t i = 0; i < sizeof(kLanguages) / sizeof(kLanguages[0]); ++i) {
    if (code.equalsIgnoreCase(kLanguages[i].locale->code)) return i;
  }
  return 0;
}

static const LanguageEntry& find_language(const char* language_code) {
  return kLanguages[find_language_index(language_code)];
}

const char* normalize_language_code(const char* language_code) {
  return find_language(language_code).locale->code;
}

const Strings& strings(const char* language_code) {
  return *find_language(language_code).strings;
}

const LocaleProfile& locale(const char* language_code) {
  return *find_language(language_code).locale;
}

size_t language_count() {
  return sizeof(kLanguages) / sizeof(kLanguages[0]);
}

const char* language_code_at(size_t index) {
  if (index >= language_count()) index = 0;
  return kLanguages[index].locale->code;
}

const char* language_native_name_at(size_t index) {
  if (index >= language_count()) index = 0;
  return kLanguages[index].locale->native_name;
}

size_t language_index(const char* language_code) {
  return find_language_index(language_code);
}

String build_language_dropdown_options() {
  String options;
  options.reserve(64);
  for (size_t i = 0; i < language_count(); ++i) {
    if (i > 0) options += '\n';
    options += kLanguages[i].locale->native_name;
  }
  return options;
}

String build_language_options_html(const char* selected_code) {
  const char* normalized = normalize_language_code(selected_code);
  String html;
  html.reserve(128);
  for (const LanguageEntry& language : kLanguages) {
    html += "<option value=\"";
    html += language.locale->code;
    html += "\"";
    if (strcmp(normalized, language.locale->code) == 0) html += " selected";
    html += ">";
    html += language.locale->native_name;
    html += "</option>";
  }
  return html;
}

String format_short_date(
    const char* language_code, int day, const char* month_short) {
  String out = locale(language_code).short_date_pattern;
  out.replace("{d}", String(day));
  out.replace("{m}", month_short ? month_short : "");
  return out;
}

String localize_numeric_text(
    const char* language_code, const String& numeric_text) {
  String text = numeric_text;
  text.trim();
  if (!text.length()) return numeric_text;

  String normalized = text;
  normalized.replace(",", ".");
  char* end = nullptr;
  const float value = strtof(normalized.c_str(), &end);
  if (end == normalized.c_str() || !isfinite(value)) return numeric_text;
  while (*end && isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end) return numeric_text;

  if (locale(language_code).decimal_separator[0] == ',') {
    text.replace(".", ",");
  } else {
    text.replace(",", ".");
  }
  return text;
}

String format_number(
    const char* language_code,
    float value,
    uint8_t decimals,
    bool trim_trailing_zeros) {
  if (!isfinite(value)) return "--";
  const uint8_t digits = decimals > 6 ? 6 : decimals;
  String text(value, static_cast<unsigned int>(digits));
  if (trim_trailing_zeros && digits > 0) {
    while (text.endsWith("0")) text.remove(text.length() - 1);
    if (text.endsWith(".")) text.remove(text.length() - 1);
  }
  return localize_numeric_text(language_code, text);
}

static bool parse_iso_date_internal(const String& iso, int& y, int& m, int& d) {
  if (iso.length() < 10) return false;
  if (iso.charAt(4) != '-' || iso.charAt(7) != '-') return false;
  y = iso.substring(0, 4).toInt();
  m = iso.substring(5, 7).toInt();
  d = iso.substring(8, 10).toInt();
  return (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31);
}

String weather_condition_label(const char* language_code, const String& condition) {
  String key = condition;
  key.trim();
  key.toLowerCase();
  if (!key.length()) return "--";

  static const char* kKeys[] = {
      "clear-night", "cloudy", "exceptional", "fog", "hail",
      "lightning", "lightning-rainy", "partlycloudy", "pouring",
      "rainy", "snowy", "snowy-rainy", "sunny", "windy",
      "windy-variant"};
  const LocaleProfile& profile = locale(language_code);
  for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); ++i) {
    if (key == kKeys[i]) return profile.weather_conditions[i];
  }

  String text = condition;
  text.replace("-", " ");
  text.replace("_", " ");
  text.trim();
  return text.length() ? text : "--";
}

String weather_weekday_short(const char* language_code, const String& iso) {
  int y = 0, m = 0, d = 0;
  if (!parse_iso_date_internal(iso, y, m, d)) return "";

  int mm = m;
  int yy = y;
  if (mm < 3) {
    mm += 12;
    yy -= 1;
  }
  int K = yy % 100;
  int J = yy / 100;
  int h = (d + (13 * (mm + 1)) / 5 + K + (K / 4) + (J / 4) + (5 * J)) % 7;
  int dow = (h + 6) % 7;
  if (dow < 0 || dow > 6) return "";

  return String(locale(language_code).weather_weekdays_short[dow]);
}

const char* weather_month_short(const char* language_code, int month) {
  if (month < 1 || month > 12) return "";
  return locale(language_code).weather_months_short[month - 1];
}

const char* weather_today_label(const char* language_code) {
  return locale(language_code).weather_today;
}

const char* weather_tomorrow_label(const char* language_code) {
  return locale(language_code).weather_tomorrow;
}

const char* climate_tile_type_label(const char* language_code) {
  return locale(language_code).tile_type_climate;
}

const char* climate_entity_label(const char* language_code) {
  return locale(language_code).climate_entity;
}

const char* climate_target_temperature_label(const char* language_code) {
  return locale(language_code).climate_target_temperature;
}

const char* climate_target_humidity_label(const char* language_code) {
  return locale(language_code).climate_target_humidity;
}

const char* climate_heating_target_label(const char* language_code) {
  return locale(language_code).climate_heating_target;
}

const char* climate_cooling_target_label(const char* language_code) {
  return locale(language_code).climate_cooling_target;
}

const char* climate_target_heat_label(const char* language_code) {
  return locale(language_code).climate_states[8];
}

const char* climate_target_cool_label(const char* language_code) {
  return locale(language_code).climate_states[9];
}

const char* entity_state_label(
    const char* language_code, const String& state_value) {
  String state = state_value;
  state.trim();
  state.toLowerCase();
  const LocaleProfile& profile = locale(language_code);
  if (state == "unavailable") return profile.climate_states[15];
  if (state == "unknown") return profile.climate_states[16];
  return "";
}

const char* climate_state_label(
    const char* language_code, const String& mode_value, const String& action_value) {
  String mode = mode_value;
  String action = action_value;
  mode.toLowerCase();
  action.toLowerCase();
  const LocaleProfile& profile = locale(language_code);
  const char* entity_state = entity_state_label(
      language_code,
      mode == "unavailable" || mode == "unknown" ? mode : action);
  if (entity_state[0]) return entity_state;
  if (action == "heating") return profile.climate_states[0];
  if (action == "preheating") return profile.climate_states[1];
  if (action == "cooling") return profile.climate_states[2];
  if (action == "drying") return profile.climate_states[3];
  if (action == "fan") return profile.climate_states[4];
  if (action == "defrosting") return profile.climate_states[5];
  if (action == "idle") return profile.climate_states[6];
  if (mode == "off" || action == "off") return profile.climate_states[7];
  if (mode == "heat") return profile.climate_states[8];
  if (mode == "cool") return profile.climate_states[9];
  if (mode == "heat_cool") return profile.climate_states[10];
  if (mode == "auto") return profile.climate_states[11];
  if (mode == "dry") return profile.climate_states[12];
  if (mode == "fan_only") return profile.climate_states[13];
  return profile.climate_states[14];
}

const char* climate_value_label(
    const char* language_code, uint8_t index) {
  if (index >= 3) return "";
  return locale(language_code).climate_value_labels[index];
}

const char* climate_control_label(
    const char* language_code, uint8_t index) {
  if (index >= 5) return "";
  return locale(language_code).climate_control_labels[index];
}

String climate_option_label(
    const char* language_code, const String& option_value) {
  static const char* const kKeys[] = {
      "none", "eco", "away", "boost", "comfort", "home", "sleep",
      "activity", "auto", "low", "medium", "high", "on", "off",
      "top", "middle", "focus", "diffuse", "vertical", "horizontal",
      "both", "left", "center", "right", "swing", "wide"};
  String key = option_value;
  key.trim();
  key.toLowerCase();
  if (key == "heat" || key == "cool" || key == "heat_cool" ||
      key == "dry" || key == "fan_only") {
    return climate_state_label(language_code, key, "");
  }
  const LocaleProfile& profile = locale(language_code);
  for (uint8_t i = 0; i < 26; ++i) {
    if (key == kKeys[i]) return profile.climate_option_labels[i];
  }
  key.replace("_", " ");
  key.replace("-", " ");
  if (key.length()) {
    key.setCharAt(0, static_cast<char>(
        toupper(static_cast<unsigned char>(key.charAt(0)))));
  }
  return key;
}

const char* climate_mini_label(
    const char* language_code, uint8_t index) {
  if (index >= 7) return "";
  return locale(language_code).climate_mini_labels[index];
}

const char* cover_label(const char* language_code, uint8_t index) {
  if (index >= 7) return "";
  return locale(language_code).cover_labels[index];
}

const char* cover_state_label(const char* language_code,
                              const String& state_value) {
  String state = state_value;
  state.trim();
  state.toLowerCase();
  static const char* const kStates[] = {
      "open", "opening", "closed", "closing", "unavailable", "unknown"};
  const LocaleProfile& profile = locale(language_code);
  for (uint8_t index = 0; index < 6; ++index) {
    if (state == kStates[index]) return profile.cover_states[index];
  }
  return profile.cover_states[5];
}

const char* binary_sensor_label(const char* language_code, uint8_t index) {
  if (index >= 8) return "";
  return locale(language_code).binary_sensor_labels[index];
}

const char* binary_sensor_state_label(const char* language_code,
                                      const String& state_value,
                                      const String& device_class_value) {
  String state = state_value;
  state.trim();
  state.toLowerCase();
  const LocaleProfile& profile = locale(language_code);
  if (state == "unavailable") return profile.binary_sensor_states[34];
  if (state == "unknown" || (state != "on" && state != "off")) {
    return profile.binary_sensor_states[35];
  }

  const bool active = state == "on";
  String device_class = device_class_value;
  device_class.trim();
  device_class.toLowerCase();

  if (device_class == "battery") {
    return profile.binary_sensor_states[active ? 2 : 3];
  }
  if (device_class == "battery_charging") {
    return profile.binary_sensor_states[active ? 4 : 5];
  }
  if (device_class == "cold") {
    return profile.binary_sensor_states[active ? 8 : 3];
  }
  if (device_class == "connectivity") {
    return profile.binary_sensor_states[active ? 9 : 10];
  }
  if (device_class == "door" || device_class == "garage_door" ||
      device_class == "opening" || device_class == "window") {
    return profile.binary_sensor_states[active ? 11 : 12];
  }
  if (device_class == "heat") {
    return profile.binary_sensor_states[active ? 13 : 3];
  }
  if (device_class == "lock") {
    return profile.binary_sensor_states[active ? 14 : 15];
  }
  if (device_class == "moisture") {
    return profile.binary_sensor_states[active ? 16 : 17];
  }
  if (device_class == "moving") {
    return profile.binary_sensor_states[active ? 18 : 19];
  }
  if (device_class == "plug") {
    return profile.binary_sensor_states[active ? 20 : 21];
  }
  if (device_class == "power") {
    return profile.binary_sensor_states[active ? 22 : 23];
  }
  if (device_class == "presence") {
    return profile.binary_sensor_states[active ? 24 : 25];
  }
  if (device_class == "problem") {
    return profile.binary_sensor_states[active ? 26 : 27];
  }
  if (device_class == "running") {
    return profile.binary_sensor_states[active ? 28 : 29];
  }
  if (device_class == "safety") {
    return profile.binary_sensor_states[active ? 30 : 31];
  }
  if (device_class == "update") {
    return profile.binary_sensor_states[active ? 32 : 33];
  }
  if (device_class == "carbon_monoxide" || device_class == "gas" ||
      device_class == "light" || device_class == "motion" ||
      device_class == "occupancy" || device_class == "smoke" ||
      device_class == "sound" || device_class == "tamper" ||
      device_class == "vibration") {
    return profile.binary_sensor_states[active ? 6 : 7];
  }
  return profile.binary_sensor_states[active ? 0 : 1];
}

}  // namespace i18n
