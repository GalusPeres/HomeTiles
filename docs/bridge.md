# Bridge Integration

The [HomeTiles Bridge](https://github.com/GalusPeres/HomeTiles-Bridge) connects your displays to Home Assistant over MQTT. It shares entity data and sends tile actions back to Home Assistant, with each display listed as its own device.

<figure class="ht-screenshot">
<img src="../images/bridge-devices.png" alt="Bridge integration with three panels" width="1328" height="918" loading="lazy">
<figcaption>Displays in the HomeTiles Bridge integration</figcaption>
</figure>

<a id="requirements"></a>

## Installation

You need Home Assistant 2025.11 or newer, an MQTT broker, and HomeTiles Bridge v0.6.44 or newer for all documented tile features with firmware v0.6.10. Follow the [Home Assistant setup guide](home-assistant-setup.md) to install the Bridge and pair your first display.

<a id="via-hacs-recommended"></a>
<a id="manual"></a>
<a id="adding-a-panel"></a>

HACS provides Bridge updates. After an update, restart Home Assistant. For additional displays, follow [Multiple displays](home-assistant-setup.md#multiple-displays).

## Configuration

Open **Settings → Devices & Services → HomeTiles Bridge → Configure**.

### Panel Settings

| Field | Setting |
| --- | --- |
| Base topic | Must match **Device topic base** in the display's MQTT settings. Each display needs a unique value. |
| HA prefix | Default: `ha/statestream`. Use the same value on the Bridge and all displays. |
| Device name / manufacturer / model | Optional details shown in Home Assistant. |

### Entity Configuration { data-toc-label="Entities" }

Select the sensors, binary sensors, numbers, selects, date/time entities, weather, lights, switchable entities, covers, climate entities, media players, cameras, and scenes/scripts/buttons you want to use. Then assign them to tiles in the [Web Admin](web-admin.md).

Selections are shared across all displays. Action aliases are generated automatically and remain stable when selections are reordered; custom aliases use one `alias=entity_id` per line.

**Switches / switchable entities** accepts `switch`, `input_boolean`, `automation`, `fan`, `humidifier`, `remote`, and `siren`. They use the existing Switch tile and its on/off popup; lights retain brightness/color controls. Automations are enabled/disabled. Fan and Siren require HA support for both on and off.

**Scenes / Scripts / Buttons** accepts `scene`, `script`, `button`, and `input_button`. Assign these to the existing Scene tile: a tap activates the scene, starts the script without parameters, or presses the button once. See the [compatibility table](tiles.md#switch) for exact behavior and limits. Use updated firmware for the complete availability handling and translated editor labels.

Older display configurations, entity selections, aliases, and MQTT topic names remain valid. Commands must target selected entities; retained switch/action commands are ignored on reconnect.

**Numbers** accepts `number` and `input_number`; **Selects** accepts `select` and `input_select`; **Date/Time** accepts `time`, `date`, `datetime`, and `input_datetime`. Use their dedicated [editable tile types](tiles.md#number). State, writable limits, options, availability, and history come from Home Assistant. Recorder exclusions also apply to these history views.

Bridge v0.6.44 remains compatible with older firmware and existing configurations. New editable controls require the new firmware; no reset or re-pairing is needed for a normal update.

## Control the Displayed View

Each compatible display exposes a **View** Select entity (**Ansicht** in German) on its existing Home Assistant device, next to the display controls. It offers **Home**, configured folders including nested folders, and configured tiles with supported popups.

Choosing an option opens that target using the existing display navigation and popup. Manual navigation and closing a popup report the actual view back to HA. Opening another popup closes the previous one and releases its resources, including a camera stream. Folder PIN protection remains in effect.

Options contain the folder path and a stable target suffix, so identical names remain distinguishable. Renaming or moving a tile updates its label; deleting it removes the option. Titles with line breaks remain two lines on the display but become one line in this list. Update an automation's option text if you rename or move its target.

For example, use these actions after your doorbell trigger. Replace the Select entity and camera option with the exact values shown by your own display:

```yaml
actions:
  - action: select.select_option
    target:
      entity_id: select.hall_display_view
    data:
      option: "Home / Entrance camera [t:12]"
  - delay: "00:00:20"
  - action: select.select_option
    target:
      entity_id: select.hall_display_view
    data:
      option: "Home"
```

Commands to offline displays or missing targets are rejected. Status updates do not navigate, and old commands are not replayed after a restart or MQTT reconnection. Camera targets are available only on ESP32-P4.

### Energy Dashboard

Enable the electricity, gas, or water categories you need. Each requires the corresponding data in Home Assistant's [Energy Dashboard](https://my.home-assistant.io/redirect/energy/). These selections are also shared across displays.

## Local Hardware Entities { data-toc-label="Local I/O" }

Configure GPIO switches, onboard relays, and DS18B20 inputs on the display's [I/O tab](hardware-io.md). The Bridge adds them to that display's Home Assistant device automatically; they do not belong in the shared entity selection.

Saving an assignment announces it again. Deleting it makes the old Home Assistant entity unavailable. Names you changed manually in Home Assistant are preserved.

### Retired Automatic Sensors

Bridge migration removes obsolete integration-owned battery and external-temperature entries only when they do not match the display's reported capabilities. It also cleans the corresponding shared selections and configuration, so ordinary restart or re-pairing does not recreate them. Devices without implemented battery measurement no longer advertise a battery sensor.

Configured local I/O and supported sensors are preserved, as are explicitly selected user entities. This cleanup does not delete arbitrary entities because their name contains `Tab5`.

## Experimental Camera Transport { data-toc-label="Camera connection" }

Camera tiles are available on ESP32-P4. Allow the display to reach the Home Assistant host on TCP ports `8124`–`8131`. The Bridge sends display-sized JPEG frames directly over the local network; MQTT handles camera control.

Each open display uses its own stream. Video conversion uses Home Assistant CPU time, while snapshot cameras are limited by their source refresh rate. See [Camera troubleshooting](faq.md#the-camera-tile-asks-for-a-newer-bridge-or-never-shows-video) if no video appears.

## MQTT Topics Reference { data-toc-label="MQTT reference" }

Entity states use `<HA prefix>/<entity>/...`. The Bridge publishes them itself; Home Assistant's MQTT Statestream integration is not required.

??? info "Topic reference for debugging"
    `{id}` is the panel device ID; `<base>` is its unique base topic.

    | Topic | Direction | Purpose |
    | --- | --- | --- |
    | `<base>/stat/connected` | Display → HA | Connection status |
    | `<base>/view/catalog/*` | Display → HA | Available navigation targets |
    | `<base>/stat/view` | Display → HA | Confirmed current view |
    | `<base>/cmnd/view` | HA → Display | Navigate to a current target |
    | `<base>/cmnd/value` | Display → HA | Validated Number, Select, or Date/Time edit |
    | `<base>/stat/value` | HA → Display | Editable command result |
    | `tab5_lvgl/config/{id}/bridge` | Display → HA | Device and local I/O announcement |
    | `tab5_lvgl/config/{id}/bridge/apply` | HA → Display | Configuration |
    | `tab5_lvgl/config/{id}/bridge/icons` | HA → Display | Icon updates |
    | `tab5_lvgl/config/{id}/history/*` | Both | Sensor history |
    | `tab5_lvgl/config/{id}/weather/*` | Both | Weather forecasts |
    | `tab5_lvgl/config/{id}/energy/*` | Both | Energy data |
    | `<base>/cmnd/light` | Display → HA | Light controls |
    | `<base>/cmnd/switch` | Display → HA | Compatible on/off controls |
    | `<base>/cmnd/media` | Display → HA | Media controls |
    | `<base>/cmnd/climate` | Display → HA | Climate controls |
    | `<base>/cmnd/cover` | Display → HA | Cover controls |
    | `<base>/cmnd/scene` | Display → HA | Scene/script activation or button press |
    | `<base>/cmnd/camera` | Display → HA | Open/close a camera session |
    | `<base>/stat/camera` | HA → Display | Camera connection and status |
    | `<base>/cmnd/display_brightness` | HA → Display | Display brightness (1–100%) |
    | `<base>/stat/display_brightness` | Display → HA | Current display brightness |
    | `<base>/cmnd/screensaver_brightness` | HA → Display | Screensaver brightness (1–100%) |
    | `<base>/stat/screensaver_brightness` | Display → HA | Current screensaver brightness |
    | `<base>/cmnd/io/{channel_id}` | HA → Display | Local Switch command (`ON` / `OFF`) |
    | `<base>/stat/io/{channel_id}` | Display → HA | Local Switch or temperature state |
