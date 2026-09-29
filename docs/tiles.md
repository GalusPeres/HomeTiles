# Tile Types

Create and configure tiles in the [Web Admin](web-admin.md#creating-a-tile). Select Home Assistant entities in the [Bridge options](bridge.md#entity-configuration) first.

<a id="create-a-tile"></a>

**Home Assistant tiles** show entity data or control your devices. **Local tiles** provide clocks, text, folders, animations, and spacing.

All tiles share title, icon, color, size, and position settings. For types with a popup, choose whether a tap or long press opens it; the popup takes on the tile's icon, circle, and color. See [On-Device UI](device-ui.md#popups) for how popups behave.

Press **Enter** in the title field for a second line. The two lines share the same vertical center as a single-line title; text that does not fit ends in `...`. The display, popup header, and Web Admin preview use the same title.

**Sizes:** every tile resizes in half steps from 1×1. Sensor, Binary Sensor, Energy, Scene, Folder, Back, Clock, Camera, and Settings tiles can also be half a cell high; value tiles then show the title and value next to the icon.

**Colors:** each tile has an icon color, an icon circle, and a tile color (**Global**, **Custom**, or **From icon**). [Rules](web-admin.md#colors-and-rules) color the icon or tint the tile while an entity has a matching state, for example by temperature or by a text state.

## Home Assistant Tiles

These types show or control a Home Assistant entity that you selected in the [Bridge options](bridge.md#entity-configuration).

### Sensor

Shows a numeric or text-valued `sensor.*` entity. Configure the unit, decimals, value size, and an optional gauge with a minimum and maximum.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:156px" src="../images/tile-sensor.png" alt="Sensor tile with a temperature" width="260" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-sensor-half.png" alt="Half-height sensor tile" width="168" height="64" loading="lazy">
<figcaption>Half height</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-sensor-popup-7d.png" alt="Sensor history over seven days with a read value" width="792" height="792" loading="lazy">
<figcaption>Popup, 7 days</figcaption>
</figure>
</div>

**Popup:** the current value in the header and a **7D** or **24H** history: a graph for numbers, or a timeline and Activity list for text states. Touch the graph and move your finger to read an earlier value; the time and value appear above the graph, and the marker stays where you let go.

A local [DS18B20 input](hardware-io.md) can also supply the value without Home Assistant; local inputs have no Home Assistant history popup.

### Binary Sensor

Shows a `binary_sensor.*` entity with the matching state and icon, such as Open/Closed or Motion/Clear. Unknown and unavailable states are shown separately.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-binary-sensor.png" alt="Half-height binary sensor tile showing Detected" width="168" height="64" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-binary-sensor-popup.png" alt="Binary sensor timeline and Activity" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

**Popup:** a **7D** or **24H** state timeline and Activity list.

### Number

Changes a `number.*` or `input_number.*` entity. The tile shows its current value and unit; the popup uses the minimum, maximum, step, and input mode supplied by Home Assistant.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:156px" src="../images/tile-number.png" alt="Number tile with a temperature value" width="260" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-number-popup.png" alt="Number popup with a temperature stepper and history" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

- Slider values use a centered slider with the value above it. Release the slider to send the final value.
- Temperatures use **− / value / +**, like the Climate target control.
- Other box-mode values use a bounded roller or step buttons, depending on their range.

**Popup:** the control above a numeric history graph and Activity for **7D** or **24H**. History requires the entity to be recorded by Home Assistant.

### Select

Changes a `select.*` or `input_select.*` entity using its current Home Assistant options. The tile shows the selected value; the popup uses the same dropdown interaction as Settings.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:156px" src="../images/tile-select.png" alt="Select tile showing the selected option" width="260" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-select-popup.png" alt="Select popup with dropdown, timeline and Activity" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

**Popup:** the dropdown above a compact timeline and Activity for **7D** or **24H**. An unavailable entity or incomplete option list disables selection. To operate another display's view, select that display's [View entity](bridge.md#control-the-displayed-view) in the Bridge first.

### Date/Time

Changes `time.*`, `date.*`, `datetime.*`, or `input_datetime.*` entities. Available controls follow whether the entity contains a time, a date, or both.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:156px" src="../images/tile-datetime.png" alt="Date/Time tile showing a time" width="260" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-datetime-popup.png" alt="Date/Time popup with time rollers and Activity" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

Time uses a single visible row of large, swipeable **hh / mm / ss** values, with wraparound between 23 and 00 or 59 and 00. Date fields use step controls without a keyboard.

**Popup:** the controls above Activity for **7D** or **24H**. Home Assistant's time zone is used for date/time commands.

Number, Select, and Date/Time need HomeTiles **v0.6.10** and Bridge **v0.6.44 or newer**. Add them in [Bridge Entity Configuration](bridge.md#entity-configuration), then choose their entity, value size, and popup trigger in the Web Admin. Slider edits are sent on release; step and time edits are grouped after a short pause, and the display keeps your edit while a slow device confirms it. Unavailable entities cannot be changed.

### Energy

Shows Home Assistant **Energy Dashboard** statistics for electricity, gas, water, or cost. Enable the matching [Bridge energy category](bridge.md#energy-dashboard). Configure the energy entity, unit, decimals, and value size.

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot">
<img style="width:211px" src="../images/tile-energy-yield.png" alt="Energy tile with today's solar yield" width="352" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:156px" src="../images/tile-energy-yield-half.png" alt="Half-height energy tile with today's solar yield" width="260" height="64" loading="lazy">
<figcaption>Half height</figcaption>
</figure>
</div>

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-energy-24h.png" alt="Energy day view with a read bar" width="792" height="792" loading="lazy">
<figcaption>Popup, today</figcaption>
</figure>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-energy-7d.png" alt="Energy week view with a read bar" width="792" height="792" loading="lazy">
<figcaption>Popup, 7 days</figcaption>
</figure>
</div>

**Popup:** hourly bars for the day and daily bars for the week. Tap a bar to read its time and value; a dot marks the bar you read.

In this example, Sensor tiles show current power at the top; Energy tiles show today's totals at the bottom.

<figure class="ht-screenshot">
<img src="../images/8in-folder-pv.png" alt="Sensor tiles on top, energy tiles at the bottom" width="1308" height="828" loading="lazy">
<figcaption>Solar dashboard with sensor and energy tiles</figcaption>
</figure>

### Switch

Toggles a compatible entity and reflects its state. Choose the entity, tile style, and popup trigger.

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-light-couch.png" alt="Light tile in the light's color" width="168" height="145" loading="lazy">
<figcaption>Light</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-light-desk.png" alt="Second light tile" width="168" height="145" loading="lazy">
<figcaption>Light</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-switch-tv.png" alt="Half-height switch tile" width="168" height="65" loading="lazy">
<figcaption>Half height</figcaption>
</figure>
</div>

| Home Assistant domain | Tile action |
| --- | --- |
| `switch`, `light` | Turn on/off |
| `input_boolean` | Set a Toggle helper on/off |
| `automation` | Enable/disable the automation's triggers |
| `fan`, `humidifier`, `remote`, `siren` | Turn the entity on/off using its configured defaults |

The additional domains require Bridge v0.6.42 and the updated firmware. Select them under **Switches / switchable entities** in the Bridge; lights keep their own selector. Fan and Siren controls require both HA on/off feature flags. Missing, unavailable, or unsupported entities cannot be operated.

An [automation switch](https://www.home-assistant.io/docs/automation/services/) enables/disables the automation; it does not run its actions immediately. Turning it off follows HA's default behavior and stops running actions. Advanced fan speed, humidity, remote commands/activities, and siren tones are outside this tile's on/off controls.

Local [outputs and relays](hardware-io.md) appear in the same selector and work without Home Assistant.

**Popup for lights:** brightness, color, and color temperature; only the controls the light supports appear. The bottom icons switch views, and the power button toggles the light.

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-light-brightness.png" alt="Brightness slider" width="792" height="792" loading="lazy">
<figcaption>Brightness</figcaption>
</figure>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-light-color.png" alt="Color wheel" width="792" height="792" loading="lazy">
<figcaption>Color</figcaption>
</figure>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-light-temperature.png" alt="Color temperature" width="792" height="792" loading="lazy">
<figcaption>Color temperature</figcaption>
</figure>
</div>

**Popup for other compatible entities:** the on/off slider in the same popup.

### Cover

Controls a `cover` entity and shows its state and position when available.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:211px" src="../images/tile-cover.png" alt="Cover tile showing Open and 54 percent" width="352" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-cover-popup.png" alt="Cover popup with position and tilt sliders" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

**Popup:** separate position and tilt sliders, or open, close, and stop buttons. Controls depend on the cover's capabilities and availability.

### Scene

Runs a configured action with a tap; there is no popup. Choose the alias generated for it in the [Bridge options](bridge.md#entity-configuration).

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-scene-bright.png" alt="Scene tile Bright" width="168" height="145" loading="lazy">
<figcaption>Scene</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-scene-reading.png" alt="Scene tile Reading" width="168" height="145" loading="lazy">
<figcaption>Scene</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-scene-warm.png" alt="Scene tile Warm" width="168" height="145" loading="lazy">
<figcaption>Scene</figcaption>
</figure>
</div>

| Home Assistant domain | Tile action |
| --- | --- |
| `scene` | Activate the scene (`scene.turn_on`) |
| `script` | Start the script without additional fields (`script.turn_on`) |
| `button` | Press the button once (`button.press`) |
| `input_button` | Press the Button helper once (`input_button.press`) |

Buttons require Bridge v0.6.42 and the updated firmware. A never-pressed button remains usable even when HA reports an unknown timestamp. Missing or unavailable actions are ignored. Existing aliases remain stable when you reorder the selection or add entities with the same object name; custom aliases remain supported.

Use an HA script with defaults when an action needs parameters. Read-only `binary_sensor` and `event` entities cannot be pressed or switched. Locks, alarms, vacuums, valves, and update entities have different actions and are not mapped to these tile types; Cover, Climate, and Media keep their dedicated tiles.

### Weather

Shows current conditions and the next days from a `weather` entity.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:322px" src="../images/tile-weather.png" alt="Weather tile with current conditions and forecast" width="536" height="306" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-weather-popup.png" alt="Weather popup" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

**Popup:** temperatures, precipitation, and rain probability. Use the arrows to browse the available forecast.

**Icons:** weather icons are filled and colored (sun gold, clouds grey, rain blue, snow ice blue, thunderstorms purple, wind teal, night grey blue). Turn off **Colored weather icons** in the tile settings to keep the white outlines. With **Tile color: From icon**, the tile takes the color of the current weather; an icon color you set yourself draws the whole icon in that color. When the bridge sends sunrise and sunset, partly cloudy and clear hours after sunset show the moon.

### Media

Shows cover art, title, and playback controls for a `media_player` entity. Media tiles need at least 2×2 cells.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:211px" src="../images/tile-media.png" alt="Media tile with cover art and playback controls" width="352" height="306" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-media-popup.png" alt="Media popup" width="792" height="792" loading="lazy">
<figcaption>Popup</figcaption>
</figure>
</div>

**Popup:** title and cover art with playback controls and volume.

### Climate

Controls a `climate` entity. The icon and accent indicate active heating, cooling, drying, or fan operation.

Configure mini-tiles for temperatures, humidity, targets, and mode, or choose **Automatic**. Arrange them in the [Climate mini-tile editor](web-admin.md#editing-climate-mini-tiles). Wide target controls show the active mode, such as **Heat**, **Cool**, or **Auto**, and a humidity target is labeled **Humidity**.

<figure class="ht-screenshot">
<img style="width:653px" src="../images/8in-climate.png" alt="Climate tiles while heating, cooling, and in auto mode" width="1088" height="225" loading="lazy">
<figcaption>Climate tiles while heating, cooling, and in auto mode</figcaption>
</figure>

**Popup:** adjust the target with the dial or plus/minus buttons. Available controls may include heating/cooling targets, humidity, mode, presets, fan, and swing; longer option lists scroll.

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-climate-popup-1.png" alt="Climate popup with HVAC mode, humidity, preset, fan, and swing controls" width="792" height="792" loading="lazy">
<figcaption>Popup with all supported controls</figcaption>
</figure>
</div>

### Camera (experimental) { data-toc-label="Camera" }

Opens a 16:9 video popup on ESP32-P4. Select the camera in the Bridge's **Entity Configuration**, then assign it to this tile. The camera can also be the [built-in camera](bridge.md#built-in-camera) of another HomeTiles display.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-camera.png" alt="Camera tile" width="168" height="145" loading="lazy">
<figcaption>Tile</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-camera-popup.png" alt="Camera popup showing the live image of another display's built-in camera" width="792" height="792" loading="lazy">
<figcaption>Popup with a live image</figcaption>
</figure>
</div>

Allow local TCP access to the Home Assistant host on ports `8124`–`8131`. See [Camera connection](bridge.md#experimental-camera-transport) for requirements and performance notes. Camera tiles are unavailable on ESP32-S3.

## Local Tiles

These types work without Home Assistant.

### Clock

Shows time and date using the device's localization settings. You can override the formats and sizes for each tile and hide its border. Tap it to open the [screensaver](screensaver.md).

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot">
<img style="width:120px" src="../images/tile-clock.png" alt="Clock tile with time and date" width="200" height="126" loading="lazy">
<figcaption>Clock without a border</figcaption>
</figure>
</div>

### Text

A static label with a selectable font size. Its border can be hidden per tile.

### Folder

Opens a sub-page with its own grid and an automatic back tile. See [Folders](web-admin.md#folders) for setup and optional PIN protection.

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-folder-pv.png" alt="Folder tile PV" width="168" height="145" loading="lazy">
<figcaption>Folder</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-folder-climate.png" alt="Folder tile in the heating color of a climate entity" width="168" height="145" loading="lazy">
<figcaption>Follows a climate entity</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-back.png" alt="Back tile" width="168" height="65" loading="lazy">
<figcaption>Back tile</figcaption>
</figure>
</div>

A folder tile can follow an entity inside it: with [rules](web-admin.md#colors-and-rules) and **Entity color**, a Climate folder turns orange while heating and blue while cooling. The [back tile](web-admin.md#back-tile) has its own icon, color, border, and size.

A protected folder asks for its PIN before it opens. The PIN pad takes the folder tile's color and shows the folder's name with the state **Locked**; below **Enter PIN**, one dot appears per typed digit. The PIN has 4 to 8 digits.

<div class="ht-type-shots ht-type-pair" markdown>
<div class="ht-type-stack" markdown>
<figure class="ht-screenshot">
<img style="width:101px" src="../images/tile-folder-misc.png" alt="Protected folder tile MISC" width="168" height="146" loading="lazy">
<figcaption>Protected folder</figcaption>
</figure>
</div>
<figure class="ht-screenshot ht-popup">
<img style="width:475px" src="../images/8in-pin-popup.png" alt="PIN pad of a protected folder" width="792" height="792" loading="lazy">
<figcaption>PIN entry</figcaption>
</figure>
</div>

### Animation

Plays a `.panim` pixel animation from `/animations` on microSD. Configure frame rate, fit, and zoom.

### Empty

Leaves an empty cell for spacing.
