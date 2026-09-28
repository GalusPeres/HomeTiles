# On-Device UI

Tap tiles to control devices, open folders, or view details. Configure the layout in the [Web Admin](web-admin.md).

<figure class="ht-screenshot">
<img src="../images/8in-home.png" alt="Home dashboard" width="1308" height="828" loading="lazy">
<figcaption>HomeTiles dashboard</figcaption>
</figure>

Every icon sits on a round circle, and a tile can take on its icon's color. Tiles and circles use the same corner radius, and half-height tiles show their title and value next to the icon. Colors, circles, and radius are set in the [Web Admin](web-admin.md#global-settings); [rules](web-admin.md#colors-and-rules) can color a tile by the state of an entity. Every tile type and its popup is shown under [Tile Types](tiles.md).

## Folders

Folders have their own grid. The back tile returns to the previous page.

<figure class="ht-screenshot">
<img src="../images/8in-folder-lighting.png" alt="Folder page with light tiles and scenes" width="1308" height="828" loading="lazy">
<figcaption>Folder with light tiles and scenes</figcaption>
</figure>

A protected folder asks for its PIN first; see [Folder](tiles.md#folder).

## Popups

For tiles with detail controls, choose a tap or long press as the popup trigger in the Web Admin. Popups take on the look of their tile: the header shows the tile's icon and circle, and most popups use the tile's color.

History popups have **24H** and **7D** views; the previous data stays visible until the new period arrives. To read an earlier value, touch a graph and move your finger along it, or tap an Energy bar. The time and value appear above the graph, and the marker stays where you let go.

Each type's popup is shown with its tile:

| Tile | Popup |
| --- | --- |
| [Sensor](tiles.md#sensor), [Binary Sensor](tiles.md#binary-sensor) | History graph or state timeline with Activity |
| [Number](tiles.md#number), [Select](tiles.md#select), [Date/Time](tiles.md#datetime) | Control above the history |
| [Energy](tiles.md#energy) | Hourly and daily bars |
| [Switch](tiles.md#switch) | Brightness, color, and color temperature for lights; on/off otherwise |
| [Cover](tiles.md#cover) | Position and tilt |
| [Weather](tiles.md#weather), [Media](tiles.md#media), [Climate](tiles.md#climate) | Forecast, playback, temperature and modes |
| [Camera](tiles.md#camera-experimental) | Live video |

Home Assistant can also open a folder or popup using the display's [View Select entity](bridge.md#control-the-displayed-view).

## Built-in Camera Indicator { data-toc-label="Camera indicator" }

While Home Assistant uses the display's own [built-in camera](bridge.md#built-in-camera), a red line runs along the top edge and a **Camera active** pill appears below it. Tap the pill to end the live stream; the camera stays available for the next request. Both parts can be switched off in the [Web Admin](web-admin.md#built-in-camera).

<figure class="ht-screenshot">
<img src="../images/8in-home-camera-active.png" alt="Red line and Camera active pill at the top of the dashboard" width="1308" height="828" loading="lazy">
<figcaption>The camera is in use by Home Assistant</figcaption>
</figure>

## Settings

The gear tile opens Settings.

<figure class="ht-screenshot">
<img src="../images/8in-settings.png" alt="Settings menu" width="1308" height="828" loading="lazy">
<figcaption>Device settings menu</figcaption>
</figure>

If Settings is PIN-protected, enter the PIN to open it. If its tile is hidden, swipe inward from the configured screen edge; the swipe also works while the tile is visible. The factory PIN **466384537** also unlocks protected folders; see [Settings access](web-admin.md#settings-tile-and-access).

### Display

Adjust brightness, sleep timeout, screensaver timeout, and screensaver brightness. **Never** disables the corresponding timeout. The rotate button turns the UI by 180°.

<figure class="ht-screenshot">
<img src="../images/8in-display-popup.png" alt="Display settings with screensaver timeout" width="1272" height="792" loading="lazy">
<figcaption>Display and screensaver settings</figcaption>
</figure>

Tap a **Clock** tile to open the screensaver immediately. See [Screensaver](screensaver.md) for its layout and images.

### WiFi

Select a network and enter its password. The connected network is checked, and its IP address opens the [Web Admin](web-admin.md).

<figure class="ht-screenshot">
<img src="../images/8in-wifi-popup.png" alt="WiFi popup" width="1272" height="792" loading="lazy">
<figcaption>WiFi settings</figcaption>
</figure>

- **Disconnect:** stay offline until you reconnect or restart; saved credentials remain.
- **Enable AP:** connect through the display's hotspot and setup portal.
- **Manual:** enter the SSID and password with the on-screen keyboard.

<figure class="ht-screenshot">
<img src="../images/8in-wifi-connect.png" alt="Manual WiFi entry with on-screen keyboard" width="1272" height="792" loading="lazy">
<figcaption>WiFi entry with the on-screen keyboard</figcaption>
</figure>

### Localization

Choose English or German, time zone, date/time formats, and keyboard layout. Tile titles also support Cyrillic characters.

<figure class="ht-screenshot">
<img src="../images/8in-localization-popup.png" alt="Localization settings" width="1272" height="792" loading="lazy">
<figcaption>Language and regional settings</figcaption>
</figure>
<figure class="ht-screenshot">
<img src="../images/8in-settings-de.png" alt="Settings menu in German" width="1308" height="828" loading="lazy">
<figcaption>Device settings in German</figcaption>
</figure>

### System

View the firmware version and device name, or use the maintenance actions:

<figure class="ht-screenshot">
<img src="../images/8in-system-popup.png" alt="System popup" width="1272" height="792" loading="lazy">
<figcaption>System information and firmware update</figcaption>
</figure>

- **Updates:** find and install a new release; see [Firmware Updates](updating.md).
- **Restart:** reboot the display.
- **GitHub:** show a QR code for the project.
- **Security:** the Home Assistant connection, [encrypted Bridge commands](bridge.md#encrypted-commands) and the [Web Admin password](web-admin.md#web-admin-password). A check shows that the display is connected, and a green shield shows that commands are encrypted. **Encrypt** sets up encryption with Home Assistant by a six-digit number, **Turn off** switches it off on both sides, and **Password** removes a forgotten Web Admin password. Both ask before they act.
