<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-title-dark.png">
  <img src="docs/images/readme-title-light.png" alt="HomeTiles" height="56">
</picture>

**Touch dashboards for Home Assistant on ESP32 displays.** Lights, climate, sensors, energy, weather and more on a 4 to 10.1 inch touch screen.

<a href="https://github.com/GalusPeres/HomeTiles/releases/latest"><img src="https://img.shields.io/github/v/release/GalusPeres/HomeTiles?label=release" alt="Latest release"></a>
<a href="LICENSE"><img src="https://img.shields.io/github/license/GalusPeres/HomeTiles" alt="MIT License"></a>
<a href="https://galusperes.github.io/"><img src="https://img.shields.io/badge/docs-online-2f81f7" alt="Documentation"></a>
<a href="https://galusperes.github.io/installer/"><img src="https://img.shields.io/badge/flasher-web-26a69a" alt="Online flasher"></a>
<a href="https://buymeacoffee.com/galusperes"><img src="https://img.shields.io/badge/Buy%20me%20a%20coffee-support-FFDD00?logo=buymeacoffee&logoColor=white" alt="Buy Me a Coffee"></a>

<img src="docs/images/readme-hero.png" alt="HomeTiles home screen with sensor, energy and binary sensor popups" width="100%">

## New in v0.7.0

<img src="docs/images/readme-new.png" alt="Half-size tiles, color rules, tiles in the color of their light and folders that follow an entity" width="100%">

Half-size tiles, icon circles, color rules and adjustable corners. Folders can follow an entity, like a climate folder that turns orange while heating and blue while cooling, or a lighting folder in the color of its lights. Built-in cameras are shared with Home Assistant. Update HomeTiles Bridge to **v0.7.0** first and [export your dashboard](https://galusperes.github.io/updating/) before updating. [All changes](docs/releases/v0.7.0.md)

## Get started

You need a compatible display, Home Assistant, an MQTT broker and [HomeTiles Bridge](https://github.com/GalusPeres/HomeTiles-Bridge).

1. Find your exact model in the [device list](https://galusperes.github.io/#device-support).
2. Install it with the [online flasher](https://galusperes.github.io/installer/).
3. Connect [Home Assistant](https://galusperes.github.io/home-assistant-setup/) and [build your dashboard](https://galusperes.github.io/web-admin/) in the browser.

Camera tiles are experimental and available on ESP32-P4 only.

## Help

[Firmware updates](https://galusperes.github.io/updating/) · [Troubleshooting](https://galusperes.github.io/faq/) · [Report a problem](https://github.com/GalusPeres/HomeTiles/issues) · [Contribute](CONTRIBUTING.md) · [Architecture](ARCHITECTURE.md)

HomeTiles is free and open source under the [MIT License](LICENSE).
