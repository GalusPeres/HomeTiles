---
title: HomeTiles
---

# HomeTiles

HomeTiles is free, open-source firmware that turns a supported touch display into a Home Assistant control panel. Use tiles to switch lights, adjust heating, control music, and see sensor or energy data at a glance.

## Get started

1. [Flash the firmware](installer.md) using the online flasher. Choose your exact device and installation mode.
2. [Connect Home Assistant](home-assistant-setup.md) through the MQTT broker and HomeTiles Bridge.
3. [Configure your dashboard](web-admin.md) in the display's Web Admin panel.
4. [Use the display](device-ui.md) to control devices and view their data.

## Features

- **Controls:** lights, switches, covers, heating, media, numbers, selections and date/time values.
- **Sensors & energy:** live values, state history, energy statistics and weather.
- **Dashboard:** arrange tiles and folders with a live browser preview; open views from Home Assistant.
- **Screensaver:** display a clock, photos and sensor tiles.
- **Local hardware:** use supported GPIO outputs, relays and temperature sensors.
- **Updates:** install firmware from the display or your browser.
- **Camera:** experimental live video on ESP32-P4 displays.

<figure class="ht-screenshot">
<img src="images/8in-home-new.png" alt="HomeTiles dashboard" width="1308" height="828" loading="lazy">
<figcaption>HomeTiles dashboard</figcaption>
</figure>

<figure class="ht-screenshot">
<img src="images/8in-screensaver.png" alt="Clock and sensor screensaver" width="1308" height="828" loading="lazy">
<figcaption>Screensaver with clock and sensor tiles</figcaption>
</figure>

[All tile types](tiles.md) and [screensaver configuration](screensaver.md).

## Demo

<figure class="ht-screenshot">
<video class="ht-demo" controls playsinline preload="metadata" poster="images/hometiles-demo-poster.jpg" aria-label="HomeTiles device demo">
<source src="videos/hometiles-demo.mp4" type="video/mp4">
</video>
<figcaption>HomeTiles device demo</figcaption>
</figure>

## Device Support

Match the exact hardware revision before flashing. [Open online flasher](installer.md#browser-installer).

### ESP32-P4

<div class="ht-device-table" markdown>

| Model | Display | Status | Link |
| --- | --- | --- | --- |
| M5Stack Tab5 | 5" / 1280×720 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-1" aria-controls="device-note-1" aria-expanded="false" aria-label="Support details for M5Stack Tab5"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://shop.m5stack.com/products/m5stack-tab5-iot-development-kit-esp32-p4) |
| Waveshare LCD-4B (P4) | 4" / 720×720 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-2" aria-controls="device-note-2" aria-expanded="false" aria-label="Support details for Waveshare LCD-4B (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm) |
| Waveshare 86 Panel ETH-2RO | 4" / 720×720 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-3" aria-controls="device-note-3" aria-expanded="false" aria-label="Support details for Waveshare 86 Panel ETH-2RO"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.net/shop/ESP32-P4-86-Panel-ETH-2RO.htm) |
| Waveshare LCD-8 (P4) | 8" / 1280×800 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-4" aria-controls="device-note-4" aria-expanded="false" aria-label="Support details for Waveshare LCD-8 (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) |
| Guition JC8012P4A1 V1 | 10.1" / 1280×800 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-5" aria-controls="device-note-5" aria-expanded="false" aria-label="Support details for Guition JC8012P4A1 V1"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/hmi-display-panel) |
| Waveshare LCD-4.3 (P4) | 4.3" / 800×480 | <span class="ht-device-state">Partial <button type="button" class="ht-device-info" popovertarget="device-note-6" aria-controls="device-note-6" aria-expanded="false" aria-label="Support details for Waveshare LCD-4.3 (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm) |
| Waveshare LCD-7 (P4) | 7" / 1280×720 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-7" aria-controls="device-note-7" aria-expanded="false" aria-label="Support details for Waveshare LCD-7 (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) |
| Waveshare 7B/7B-C (P4 pre-v3) | 7" / 1024×600 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-8" aria-controls="device-note-8" aria-expanded="false" aria-label="Support details for Waveshare 7B/7B-C (P4 pre-v3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7B.htm) |
| Waveshare 7B/7B-C (P4 v3.1) | 7" / 1024×600 | <span class="ht-device-state">Experimental <button type="button" class="ht-device-info" popovertarget="device-note-9" aria-controls="device-note-9" aria-expanded="false" aria-label="Support details for Waveshare 7B/7B-C (P4 v3.1)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7B.htm) |
| Waveshare LCD-10.1 (P4) | 10.1" / 1280×800 | <span class="ht-device-state">Partial <button type="button" class="ht-device-info" popovertarget="device-note-10" aria-controls="device-note-10" aria-expanded="false" aria-label="Support details for Waveshare LCD-10.1 (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) |
| Guition JC8012P4A1 V2 | 10.1" / 1280×800 | <span class="ht-device-state">Partial <button type="button" class="ht-device-info" popovertarget="device-note-11" aria-controls="device-note-11" aria-expanded="false" aria-label="Support details for Guition JC8012P4A1 V2"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/hmi-display-panel) |
| Guition JC1060P470C V1 | 7" / 1024×600 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-12" aria-controls="device-note-12" aria-expanded="false" aria-label="Support details for Guition JC1060P470C V1"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/7-inch-esp32p4-display-module) |
| Guition JC1060P470C V2 | 7" / 1024×600 | <span class="ht-device-state">Partial <button type="button" class="ht-device-info" popovertarget="device-note-13" aria-controls="device-note-13" aria-expanded="false" aria-label="Support details for Guition JC1060P470C V2"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/7-inch-esp32p4-display-module) |

</div>

### ESP32-S3

<div class="ht-device-table" markdown>

| Model | Display | Status | Link |
| --- | --- | --- | --- |
| Guition ESP32-4848S040C_I | 4" / 480×480 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-14" aria-controls="device-note-14" aria-expanded="false" aria-label="Support details for Guition ESP32-4848S040C_I"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32-display-module/4-inch-esp32s3-display-module) |
| Waveshare LCD-4 Rev 4.0 (S3) | 4" / 480×480 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-15" aria-controls="device-note-15" aria-expanded="false" aria-label="Support details for Waveshare LCD-4 Rev 4.0 (S3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/product/esp32-s3-touch-lcd-4.htm) |
| Waveshare LCD-4B (S3) | 4" / 480×480 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-16" aria-controls="device-note-16" aria-expanded="false" aria-label="Support details for Waveshare LCD-4B (S3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-s3-touch-lcd-4b.htm) |

</div>

<div class="ht-device-note" id="device-note-1" popover="auto" role="dialog" aria-labelledby="device-note-1-title"><div class="ht-device-note-title" id="device-note-1-title">M5Stack Tab5</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer.</p></div>

<div class="ht-device-note" id="device-note-2" popover="auto" role="dialog" aria-labelledby="device-note-2-title"><div class="ht-device-note-title" id="device-note-2-title">Waveshare LCD-4B (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer. A brief blue flash while saving or updating is a known display limitation.</p><p class="ht-device-note-links"><a href="faq/#the-display-briefly-flashes-blue-when-saving-or-updating-waveshare">Display note</a></p></div>

<div class="ht-device-note" id="device-note-3" popover="auto" role="dialog" aria-labelledby="device-note-3-title"><div class="ht-device-note-title" id="device-note-3-title">Waveshare 86 Panel ETH-2RO</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware operation and native Ethernet are confirmed. Uses the ESP32-P4 LCD-4B firmware.</p></div>

<div class="ht-device-note" id="device-note-4" popover="auto" role="dialog" aria-labelledby="device-note-4-title"><div class="ht-device-note-title" id="device-note-4-title">Waveshare LCD-8 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer. A brief blue flash while saving or updating is a known display limitation.</p><p class="ht-device-note-links"><a href="faq/#the-display-briefly-flashes-blue-when-saving-or-updating-waveshare">Display note</a></p></div>

<div class="ht-device-note" id="device-note-5" popover="auto" role="dialog" aria-labelledby="device-note-5-title"><div class="ht-device-note-title" id="device-note-5-title">Guition JC8012P4A1 V1</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>v0.6.10 includes the V1-specific SDIO receive fix. Camera streams and Web Admin OTA were confirmed stable in the integrated v0.6.9b1 test build. Camera remains experimental.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/30">Issue #30</a><a href="https://github.com/GalusPeres/HomeTiles/issues/30#issuecomment-5530898054">Test build</a></p></div>

<div class="ht-device-note" id="device-note-6" popover="auto" role="dialog" aria-labelledby="device-note-6-title"><div class="ht-device-note-title" id="device-note-6-title">Waveshare LCD-4.3 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Display, touch, Wi-Fi, MQTT, Home Assistant and Weather are verified. microSD and OTA checks are still pending.</p></div>

<div class="ht-device-note" id="device-note-7" popover="auto" role="dialog" aria-labelledby="device-note-7-title"><div class="ht-device-note-title" id="device-note-7-title">Waveshare LCD-7 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Firmware is available, but hardware validation is still requested. This 1280 × 720 model uses different firmware from LCD-7B.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a></p></div>

<div class="ht-device-note" id="device-note-8" popover="auto" role="dialog" aria-labelledby="device-note-8-title"><div class="ht-device-note-title" id="device-note-8-title">Waveshare 7B/7B-C (P4 pre-v3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>A 7B-C was reported working with v0.6.8, without an exact chip revision in the report. Confirmation for this pre-v3 profile is still needed.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a><a href="https://github.com/GalusPeres/HomeTiles/issues/7#issuecomment-5398279624">Test report</a></p></div>

<div class="ht-device-note" id="device-note-9" popover="auto" role="dialog" aria-labelledby="device-note-9-title"><div class="ht-device-note-title" id="device-note-9-title">Waveshare 7B/7B-C (P4 v3.1)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Dedicated firmware for exact ESP32-P4 v3.1. Hardware validation for this revision is still pending; v3.2 and newer are unsupported.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a></p></div>

<div class="ht-device-note" id="device-note-10" popover="auto" role="dialog" aria-labelledby="device-note-10-title"><div class="ht-device-note-title" id="device-note-10-title">Waveshare LCD-10.1 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Display, touch, Wi-Fi, MQTT and OTA were reported working. Corrected defaults, SD and Camera still need release validation.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a></p></div>

<div class="ht-device-note" id="device-note-11" popover="auto" role="dialog" aria-labelledby="device-note-11-title"><div class="ht-device-note-title" id="device-note-11-title">Guition JC8012P4A1 V2</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Display, touch, brightness and MQTT are confirmed. OTA reliability was improved in v0.6.6; microSD remains untested.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/18">Issue #18</a></p></div>

<div class="ht-device-note" id="device-note-12" popover="auto" role="dialog" aria-labelledby="device-note-12-title"><div class="ht-device-note-title" id="device-note-12-title">Guition JC1060P470C V1</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Touch, brightness, SD, Wi-Fi/MQTT and OTA were confirmed working since v0.6.5. Requires the exact _I_W_Y model.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/8">Issue #8</a></p></div>

<div class="ht-device-note" id="device-note-13" popover="auto" role="dialog" aria-labelledby="device-note-13-title"><div class="ht-device-note-title" id="device-note-13-title">Guition JC1060P470C V2</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Basic operation is confirmed. v0.6.8 includes the orientation correction; full release and OTA confirmation remain pending.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/27">Issue #27</a></p></div>

<div class="ht-device-note" id="device-note-14" popover="auto" role="dialog" aria-labelledby="device-note-14-title"><div class="ht-device-note-title" id="device-note-14-title">Guition ESP32-4848S040C_I</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer. Camera tiles are unavailable; a brief black screen while saving is expected.</p></div>

<div class="ht-device-note" id="device-note-15" popover="auto" role="dialog" aria-labelledby="device-note-15-title"><div class="ht-device-note-title" id="device-note-15-title">Waveshare LCD-4 Rev 4.0 (S3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>First released in v0.6.10. Display, backlight, touch, Wi-Fi, Web Admin, MQTT and Web OTA are contributor-tested. Rev 4.0 only; Camera tiles and SD access are unavailable.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/pull/35">PR #35</a></p></div>

<div class="ht-device-note" id="device-note-16" popover="auto" role="dialog" aria-labelledby="device-note-16-title"><div class="ht-device-note-title" id="device-note-16-title">Waveshare LCD-4B (S3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Initial hardware testing was reported with PR #29. The adapted release profile still needs confirmation. No microSD or Camera tiles.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/26">Issue #26</a><a href="https://github.com/GalusPeres/HomeTiles/pull/29">PR #29</a></p></div>

<script src="javascripts/device-status.js?v=2" defer></script>

## New In v0.6.10

- **Editable tiles:** Number, Select and Date/Time controls with history, plus more switchable entities and button actions.
- **Dashboard:** Home Assistant View control, two-line titles, configurable value sizes and matching live previews.
- **Hardware:** Waveshare S3 LCD-4 Rev 4.0 support and the confirmed Guition JC8012 V1 Camera/Web OTA transport fix.
- **Bridge:** use v0.6.44 or newer through HACS. Camera remains experimental and ESP32-P4-only.

[Read the v0.6.10 release notes](releases/v0.6.10.md)

## How It Works

The display exchanges states and commands with an MQTT broker. [HomeTiles Bridge](bridge.md) connects the broker to Home Assistant, publishes entity data and executes commands from the display.

Firmware and Bridge are MIT-licensed. [Support HomeTiles](https://buymeacoffee.com/galusperes).
