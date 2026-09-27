<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-brand-dark.png">
  <img src="docs/images/readme-brand-light.png" alt="HomeTiles" height="73">
</picture>

Touch dashboards for Home Assistant on ESP32 displays.<br>
Lights, climate, sensors, energy, weather and more on a 4 to 10.1 inch touch screen.

<a href="https://galusperes.github.io/"><img src="docs/images/readme-link-docs.png" alt="Documentation" height="48"></a>
<a href="https://galusperes.github.io/installer/"><img src="docs/images/readme-link-flasher.png" alt="Online flasher" height="48"></a>
<a href="https://github.com/GalusPeres/HomeTiles/releases/latest"><img src="docs/images/readme-link-release.png" alt="Latest release" height="48"></a>
<a href="https://buymeacoffee.com/galusperes"><img src="docs/images/readme-link-coffee.png" alt="Buy Me a Coffee" height="48"></a>
<a href="LICENSE"><img src="docs/images/readme-link-license.png" alt="MIT License" height="48"></a>
<br>
<img src="docs/images/readme-hero-home.png" alt="HomeTiles home screen with sensor, energy and binary sensor popups" width="100%"><br>
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
<img src="docs/images/readme-new-lights.png" alt="Light tiles in the color of their light" width="48%">
<img src="docs/images/readme-new-folders.png" alt="Climate and lighting folders colored by another entity" width="48%"><br>
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-caption-colors-dark.svg">
  <img src="docs/images/readme-caption-colors-light.svg" alt="Entity color · Other entity" width="97%">
</picture>
</p>

- **[Half-size tiles](https://galusperes.github.io/web-admin/#moving-resizing-copying):** place and size tiles in half steps.
- **[Color rules per tile](https://galusperes.github.io/web-admin/#icon-color-by-state):** icon and tile change color by value or state, e.g. a waste tile turns red on collection day.
- **[Entity color](https://galusperes.github.io/web-admin/#colors-and-rules):** a rule can show the entity's own color, like the color of a light.
- **[Other entity](https://galusperes.github.io/web-admin/#colors-and-rules):** a rule can follow any entity, e.g. a climate folder turns orange while heating.
- **[Graph readout](https://galusperes.github.io/tiles/#sensor):** slide through sensor and energy history.
- **[Built-in cameras](https://galusperes.github.io/web-admin/#built-in-camera):** ESP32-P4 displays stream to Home Assistant.

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
