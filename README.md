<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-brand-dark.png">
  <img src="docs/images/readme-brand-light.png" alt="HomeTiles" height="73">
</picture>

Touch dashboards for Home Assistant on ESP32 displays.<br>
Lights, climate, sensors, energy, weather and more on a 4 to 10.1 inch touch screen.

<a href="https://galusperes.github.io/"><img src="docs/images/readme-nav-docs.png" alt="Documentation" height="42"></a>
<a href="https://galusperes.github.io/installer/"><img src="docs/images/readme-nav-flasher.png" alt="Online flasher" height="42"></a>
<a href="https://github.com/GalusPeres/HomeTiles/releases/latest"><img src="docs/images/readme-nav-release-v080.png" alt="Latest release" height="42"></a>
<a href="https://buymeacoffee.com/galusperes"><img src="docs/images/readme-nav-coffee.png" alt="Buy Me a Coffee" height="42"></a>
<a href="LICENSE"><img src="docs/images/readme-nav-license.png" alt="MIT License" height="42"></a>
<br>
<img src="docs/images/readme-hero-v080.png" alt="HomeTiles home screen on a 10.1 inch display" width="100%"><br>
Every tile opens a popup with its controls and history.

</div>

## Screenshots

### Popups

<img src="docs/images/readme-popups-12.png" alt="Popups for lights, climate, fan, cover, media, weather, sensor history, energy, lock, PIN, alarm panel and camera" width="100%">

### Tiles

<img src="docs/images/gallery-tiles.png" alt="Tiles for weather, media, alarm panel, lock, lights, fan, climate, sensors, folders and scenes" width="100%">

### Web Admin

Build the dashboard in the browser; the Web Admin preview matches the display.

<img src="docs/images/readme-webadmin-v080.png" alt="Web Admin with the live preview and the color rules of the Kitchen sensor" width="100%">

## New in v0.8.0

- **Polished throughout:** faster, above all on the ESP32-S3 (LVGL 9.6, about 300 KB less flash, more free RAM), quicker popups and screensaver, camera popups at up to 30 FPS, Web Admin edits that show on the display almost at once, a refined design and many fixes.
- **[Lock, Alarm Panel and Fan tiles](https://galusperes.github.io/tiles/#fan):** with their own popups and code entry.
- **[Web Admin password](https://galusperes.github.io/web-admin/#web-admin-password)** and **[encrypted commands](https://galusperes.github.io/bridge/#encrypted-commands):** pair the display with Home Assistant by comparing a six-digit number.
- **[Redesigned Switch tile](https://galusperes.github.io/tiles/#switch):** a dimmer or switch bar across the tile, also for Cover and Climate, and half height for almost every tile.
- **[French and Polish](https://galusperes.github.io/device-ui/#localization):** two new languages, with their letters on the keyboard.
- **New boards:** Guition JC8012P4A1 V3 and Waveshare 7B with ESP32-P4 v3.x.

Before updating, update HomeTiles Bridge to **v0.8.0** and [export your dashboard](https://galusperes.github.io/updating/). [All changes](docs/releases/v0.8.0.md)

## Get started

You need a compatible display, Home Assistant, an MQTT broker and [HomeTiles Bridge](https://github.com/GalusPeres/HomeTiles-Bridge).

1. Find your exact model in the [device list](https://galusperes.github.io/#device-support).
2. Install it with the [online flasher](https://galusperes.github.io/installer/).
3. Connect [Home Assistant](https://galusperes.github.io/home-assistant-setup/) and [build your dashboard](https://galusperes.github.io/web-admin/) in the browser.

Camera tiles are experimental and available on ESP32-P4 only.

## Help

[Firmware updates](https://galusperes.github.io/updating/) · [Troubleshooting](https://galusperes.github.io/faq/) · [Report a problem](https://github.com/GalusPeres/HomeTiles/issues) · [Contribute](CONTRIBUTING.md) · [Architecture](ARCHITECTURE.md)

HomeTiles is free and open source under the [MIT License](LICENSE).
