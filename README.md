<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-logo-dark.png">
  <img src="docs/images/readme-logo-light.png" alt="HomeTiles" height="63">
</picture>

Touch dashboards for Home Assistant on ESP32 displays.<br>
Lights, climate, sensors, energy, weather and more on a 4 to 10.1 inch touch screen.

<a href="https://galusperes.github.io/"><img src="docs/images/readme-pill-docs.png" alt="Documentation" height="40"></a>
<a href="https://galusperes.github.io/installer/"><img src="docs/images/readme-pill-flasher.png" alt="Online flasher" height="40"></a>
<a href="https://github.com/GalusPeres/HomeTiles/releases/latest"><img src="docs/images/readme-pill-release.png" alt="Latest release" height="40"></a>
<a href="https://buymeacoffee.com/galusperes"><img src="docs/images/readme-pill-coffee.png" alt="Buy Me a Coffee" height="40"></a>
<br>
<img src="docs/images/readme-start.png" alt="HomeTiles home screen with sensor, energy and binary sensor popups" width="100%"><br>
Tap a tile for its popup, then slide through the history of sensors, energy and binary sensors.

</div>

## New in v0.7.0

<p align="center">
<img src="docs/images/readme-new-half.png" alt="Half-size tiles" width="48%">
<img src="docs/images/readme-new-rules.png" alt="Color rules" width="48%"><br>
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-caption-row1-dark.svg">
  <img src="docs/images/readme-caption-row1-light.svg" alt="Half-size tiles · Color rules" width="97%">
</picture>
</p>

<p align="center">
<img src="docs/images/readme-new-lights.png" alt="Colors from your lights" width="48%">
<img src="docs/images/readme-new-folders.png" alt="Folders that follow an entity" width="48%"><br>
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-caption-row2-dark.svg">
  <img src="docs/images/readme-caption-row2-light.svg" alt="Colors from your lights · Folders that follow an entity" width="97%">
</picture>
</p>

- **Half-size tiles:** place and size tiles in half steps.
- **Color rules:** tiles change color by value or state.
- **Light colors:** light tiles show the color of the light.
- **Folders follow an entity:** a climate folder turns orange while heating.
- **Graph readout:** slide through sensor and energy history.
- **Built-in cameras:** ESP32-P4 displays stream to Home Assistant.

Before updating, update HomeTiles Bridge to **v0.7.0** and [export your dashboard](https://galusperes.github.io/updating/). [All changes](docs/releases/v0.7.0.md)

## Get started

You need a compatible display, Home Assistant, an MQTT broker and [HomeTiles Bridge](https://github.com/GalusPeres/HomeTiles-Bridge).

1. Find your exact model in the [device list](https://galusperes.github.io/#device-support).
2. Install it with the [online flasher](https://galusperes.github.io/installer/).
3. Connect [Home Assistant](https://galusperes.github.io/home-assistant-setup/) and [build your dashboard](https://galusperes.github.io/web-admin/) in the browser.

Camera tiles are experimental and available on ESP32-P4 only.

## Help

[Firmware updates](https://galusperes.github.io/updating/) · [Troubleshooting](https://galusperes.github.io/faq/) · [Report a problem](https://github.com/GalusPeres/HomeTiles/issues) · [Contribute](CONTRIBUTING.md) · [Architecture](ARCHITECTURE.md)

HomeTiles is free and open source under the [MIT License](LICENSE).
