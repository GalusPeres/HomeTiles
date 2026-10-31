---
title: HomeTiles
---

# HomeTiles

HomeTiles is free, open-source firmware that turns a supported touch display into a Home Assistant control panel. Use tiles to switch lights, adjust heating, control music, and see sensor or energy data at a glance.

<div class="ht-hero-switch">
<input type="radio" name="ht-hero" id="ht-hero-dark" checked>
<input type="radio" name="ht-hero" id="ht-hero-light">
<input type="radio" name="ht-hero" id="ht-hero-upright">
<div class="ht-hero-tabs">
<label for="ht-hero-dark">Dark</label>
<label for="ht-hero-light">Light</label>
<label for="ht-hero-upright">Upright</label>
</div>
<figure class="ht-screenshot ht-hero-shot ht-hero-dark">
<img src="images/v09-home.png" alt="HomeTiles Home page with status chips, clock, weather, lights, music, a camera and folders" width="1280" height="800">
<figcaption>HomeTiles on a 10-inch display</figcaption>
</figure>
<figure class="ht-screenshot ht-hero-shot ht-hero-light">
<img src="images/v09-home-light.png" alt="The same Home page in the light theme" width="1280" height="800" loading="lazy">
<figcaption>The light theme</figcaption>
</figure>
<figure class="ht-screenshot ht-hero-shot ht-hero-upright">
<img src="images/v09-upright.png" alt="The Home page on a display mounted upright" width="800" height="1280" loading="lazy">
<figcaption>Mounted upright</figcaption>
</figure>
</div>

## Get Started { #get-started }

<div class="ht-start-grid">
<a class="ht-start-card" href="installer/#browser-installer"><span class="ht-start-num">1</span><strong>Flash the firmware</strong><span>From your browser</span></a>
<a class="ht-start-card" href="installer/#install-the-bridge"><span class="ht-start-num">2</span><strong>Install the Bridge</strong><span>In Home Assistant</span></a>
<a class="ht-start-card" href="installer/#set-up-the-display"><span class="ht-start-num">3</span><strong>Set up the display</strong><span>It guides you and pairs</span></a>
<a class="ht-start-card" href="installer/#add-your-tiles"><span class="ht-start-num">4</span><strong>Add your tiles</strong><span>In the Web Admin</span></a>
</div>

<a id="features"></a>

## What It Looks Like { #what-it-looks-like }

<figure class="ht-screenshot">
<img src="images/v09-tiles.png" alt="Tiles for weather, lights, a fan, sensors, a vacuum, a lock, a coffee machine and status chips" width="1280" height="440" loading="lazy">
<figcaption>Tiles for your devices</figcaption>
</figure>

<figure class="ht-screenshot">
<img src="images/v09-popups.png" alt="Popups for a light, climate, weather, sensor history, a lock and media" width="1248" height="824" loading="lazy">
<figcaption>A tap opens the details</figcaption>
</figure>

<figure class="ht-screenshot">
<img src="images/v09-overview.png" alt="Page overview with pictures of Home, Lights, Climate and House" width="1280" height="800" loading="lazy">
<figcaption>Pages you swipe through</figcaption>
</figure>

<figure class="ht-screenshot">
<img src="images/v09-screensaver.png" alt="Screensaver with a mountain photo, the time and date, and sensor tiles" width="1280" height="800" loading="lazy">
<figcaption>The screensaver</figcaption>
</figure>

<div class="ht-type-shots" markdown>
<figure class="ht-screenshot">
<img style="width:340px" src="images/v09-home-light.png" alt="Home page in the light theme" width="1280" height="800" loading="lazy">
<figcaption>Light theme</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:136px" src="images/v09-upright.png" alt="Upright Home page" width="800" height="1280" loading="lazy">
<figcaption>Upright</figcaption>
</figure>
<figure class="ht-screenshot">
<img style="width:215px" src="images/v09-4b.png" alt="Home page on a 4-inch square display" width="720" height="720" loading="lazy">
<figcaption>4-inch square</figcaption>
</figure>
</div>

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
| Waveshare LCD-5 (P4 pre-v3) | 5" / 1280×720 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-19" aria-controls="device-note-19" aria-expanded="false" aria-label="Support details for Waveshare LCD-5 (P4 pre-v3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-5.htm) |
| Waveshare LCD-5 (P4 v3.x) | 5" / 1280×720 | <span class="ht-device-state">Experimental <button type="button" class="ht-device-info" popovertarget="device-note-20" aria-controls="device-note-20" aria-expanded="false" aria-label="Support details for Waveshare LCD-5 (P4 v3.x)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-5.htm) |
| Waveshare LCD-7 (P4) | 7" / 1280×720 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-7" aria-controls="device-note-7" aria-expanded="false" aria-label="Support details for Waveshare LCD-7 (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) |
| Waveshare 7B/7B-C (P4 pre-v3) | 7" / 1024×600 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-8" aria-controls="device-note-8" aria-expanded="false" aria-label="Support details for Waveshare 7B/7B-C (P4 pre-v3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7B.htm) |
| Waveshare 7B/7B-C (P4 v3.x) | 7" / 1024×600 | <span class="ht-device-state">Experimental <button type="button" class="ht-device-info" popovertarget="device-note-9" aria-controls="device-note-9" aria-expanded="false" aria-label="Support details for Waveshare 7B/7B-C (P4 v3.x)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7B.htm) |
| Elecrow CrowPanel Advanced 10.1 (P4 pre-v3) | 10.1" / 1024×600 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-22" aria-controls="device-note-22" aria-expanded="false" aria-label="Support details for Elecrow CrowPanel Advanced 10.1 (P4 pre-v3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.elecrow.com/crowpanel-advanced-10-1-inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen.html) |
| Waveshare LCD-10.1 (P4) | 10.1" / 1280×800 | <span class="ht-device-state">Partial <button type="button" class="ht-device-info" popovertarget="device-note-10" aria-controls="device-note-10" aria-expanded="false" aria-label="Support details for Waveshare LCD-10.1 (P4)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-7-8-10.1.htm) |
| Guition JC8012P4A1 V2 | 10.1" / 1280×800 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-11" aria-controls="device-note-11" aria-expanded="false" aria-label="Support details for Guition JC8012P4A1 V2"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/hmi-display-panel) |
| Guition JC8012P4A1 V3 | 10.1" / 1280×800 | <span class="ht-device-state">Experimental <button type="button" class="ht-device-info" popovertarget="device-note-18" aria-controls="device-note-18" aria-expanded="false" aria-label="Support details for Guition JC8012P4A1 V3"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/hmi-display-panel) |
| Guition JC1060P470C V1 | 7" / 1024×600 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-12" aria-controls="device-note-12" aria-expanded="false" aria-label="Support details for Guition JC1060P470C V1"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/7-inch-esp32p4-display-module) |
| Guition JC1060P470C V2 | 7" / 1024×600 | <span class="ht-device-state">Partial <button type="button" class="ht-device-info" popovertarget="device-note-13" aria-controls="device-note-13" aria-expanded="false" aria-label="Support details for Guition JC1060P470C V2"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/7-inch-esp32p4-display-module) |
| Guition JC4880P443 | 4.3" / 480×800 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-17" aria-controls="device-note-17" aria-expanded="false" aria-label="Support details for Guition JC4880P443"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32p4-display-module/esp32p4-display) |

</div>

### ESP32-S3

<div class="ht-device-table" markdown>

| Model | Display | Status | Link |
| --- | --- | --- | --- |
| Guition ESP32-4848S040C_I | 4" / 480×480 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-14" aria-controls="device-note-14" aria-expanded="false" aria-label="Support details for Guition ESP32-4848S040C_I"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.guition.com/esp32-display-module/4-inch-esp32s3-display-module) |
| Waveshare LCD-4 Rev 4.0 (S3) | 4" / 480×480 | <span class="ht-device-state">Tested <button type="button" class="ht-device-info" popovertarget="device-note-15" aria-controls="device-note-15" aria-expanded="false" aria-label="Support details for Waveshare LCD-4 Rev 4.0 (S3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/product/esp32-s3-touch-lcd-4.htm) |
| Waveshare LCD-4B (S3) | 4" / 480×480 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-16" aria-controls="device-note-16" aria-expanded="false" aria-label="Support details for Waveshare LCD-4B (S3)"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://www.waveshare.com/esp32-s3-touch-lcd-4b.htm) |
| Golden Security GS-T3E v2 | 4" / 480×480 | <span class="ht-device-state">Pending <button type="button" class="ht-device-info" popovertarget="device-note-21" aria-controls="device-note-21" aria-expanded="false" aria-label="Support details for Golden Security GS-T3E v2"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.2" aria-hidden="true"><circle cx="8" cy="8" r="6.5"/><path d="M8 7v4"/><circle cx="8" cy="4.5" r=".7" fill="currentColor" stroke="none"/></svg></button></span> | [Link](https://openhasp.haswitchplate.com/0.7.0/hardware/golden-security/gs-t3e/) |

</div>

<div class="ht-device-note" id="device-note-1" popover="auto" role="dialog" aria-labelledby="device-note-1-title"><div class="ht-device-note-title" id="device-note-1-title">M5Stack Tab5</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer.</p></div>

<div class="ht-device-note" id="device-note-2" popover="auto" role="dialog" aria-labelledby="device-note-2-title"><div class="ht-device-note-title" id="device-note-2-title">Waveshare LCD-4B (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer. A brief blue flash while saving or updating is a known display limitation.</p><p class="ht-device-note-links"><a href="faq/#the-display-briefly-flashes-blue-when-saving-or-updating-waveshare">Display note</a></p></div>

<div class="ht-device-note" id="device-note-3" popover="auto" role="dialog" aria-labelledby="device-note-3-title"><div class="ht-device-note-title" id="device-note-3-title">Waveshare 86 Panel ETH-2RO</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware operation and native Ethernet are confirmed. Uses the ESP32-P4 LCD-4B firmware.</p></div>

<div class="ht-device-note" id="device-note-4" popover="auto" role="dialog" aria-labelledby="device-note-4-title"><div class="ht-device-note-title" id="device-note-4-title">Waveshare LCD-8 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer. A brief blue flash while saving or updating is a known display limitation.</p><p class="ht-device-note-links"><a href="faq/#the-display-briefly-flashes-blue-when-saving-or-updating-waveshare">Display note</a></p></div>

<div class="ht-device-note" id="device-note-5" popover="auto" role="dialog" aria-labelledby="device-note-5-title"><div class="ht-device-note-title" id="device-note-5-title">Guition JC8012P4A1 V1</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>v0.6.10 includes the V1-specific SDIO receive fix. Camera streams and Web Admin OTA were confirmed stable in the integrated v0.6.9b1 test build. Camera remains experimental.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/30">Issue #30</a><a href="https://github.com/GalusPeres/HomeTiles/issues/30#issuecomment-5530898054">Test build</a></p></div>

<div class="ht-device-note" id="device-note-6" popover="auto" role="dialog" aria-labelledby="device-note-6-title"><div class="ht-device-note-title" id="device-note-6-title">Waveshare LCD-4.3 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Display, touch, Wi-Fi, MQTT, Home Assistant and Weather are verified. microSD and OTA checks are still pending.</p></div>

<div class="ht-device-note" id="device-note-19" popover="auto" role="dialog" aria-labelledby="device-note-19-title"><div class="ht-device-note-title" id="device-note-19-title">Waveshare LCD-5 (P4 pre-v3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>New profile built from Waveshare's documentation for ESP32-P4 before v3.0 (for example v1.3). Shown like the Tab5 at 1280 × 720. Community validation is pending.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/76">Issue #76</a></p></div>

<div class="ht-device-note" id="device-note-20" popover="auto" role="dialog" aria-labelledby="device-note-20-title"><div class="ht-device-note-title" id="device-note-20-title">Waveshare LCD-5 (P4 v3.x)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Experimental firmware for ESP32-P4 v3.1 or newer, built from Waveshare's documentation. Check the revision with esptool chip-id first. Community validation is pending.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/76">Issue #76</a></p></div>

<div class="ht-device-note" id="device-note-7" popover="auto" role="dialog" aria-labelledby="device-note-7-title"><div class="ht-device-note-title" id="device-note-7-title">Waveshare LCD-7 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Firmware is available, but hardware validation is still requested. This 1280 × 720 model uses different firmware from LCD-7B.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a></p></div>

<div class="ht-device-note" id="device-note-8" popover="auto" role="dialog" aria-labelledby="device-note-8-title"><div class="ht-device-note-title" id="device-note-8-title">Waveshare 7B/7B-C (P4 pre-v3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>A 7B-C was reported working with v0.6.8, without an exact chip revision in the report. Confirmation for this pre-v3 profile is still needed.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a><a href="https://github.com/GalusPeres/HomeTiles/issues/7#issuecomment-5398279624">Test report</a></p></div>

<div class="ht-device-note" id="device-note-9" popover="auto" role="dialog" aria-labelledby="device-note-9-title"><div class="ht-device-note-title" id="device-note-9-title">Waveshare 7B/7B-C (P4 v3.x)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Dedicated experimental firmware for ESP32-P4 v3.1 or newer, including v3.2. Hardware validation is still pending. Boards running the former v3.1-only image need one browser installer Update to switch to it.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/41">Issue #41</a></p></div>
<div class="ht-device-note" id="device-note-22" popover="auto" role="dialog" aria-labelledby="device-note-22-title"><div class="ht-device-note-title" id="device-note-22-title">Elecrow CrowPanel Advanced 10.1 (P4 pre-v3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Initial HomeTiles port for the Elecrow CrowPanel Advanced 10.1-inch ESP32-P4 panel. Compile and runtime validation on V1.2 hardware is still pending.</p></div>

<div class="ht-device-note" id="device-note-10" popover="auto" role="dialog" aria-labelledby="device-note-10-title"><div class="ht-device-note-title" id="device-note-10-title">Waveshare LCD-10.1 (P4)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Display, touch, Wi-Fi, MQTT and OTA were reported working. Corrected defaults, SD and Camera still need release validation. From v0.7.0, boards with ESP32-P4 v3.1 or newer get a separate experimental firmware (contributor-tested on v3.2).</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/7">Issue #7</a></p></div>

<div class="ht-device-note" id="device-note-11" popover="auto" role="dialog" aria-labelledby="device-note-11-title"><div class="ht-device-note-title" id="device-note-11-title">Guition JC8012P4A1 V2</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Maintainer-tested on JC8012P4A1C_I_W_Y1, SKU:10153002-V2. Display, touch, brightness, MQTT and microSD are confirmed. The interface runs smoothly with the v0.6.12 PPA correction. The SD reboot reported on another unit in issue #55 remains under investigation.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/18">Issue #18</a> <a href="https://github.com/GalusPeres/HomeTiles/releases/tag/v0.6.12">v0.6.12</a> <a href="https://github.com/GalusPeres/HomeTiles/issues/55">Issue #55</a></p></div>

<div class="ht-device-note" id="device-note-18" popover="auto" role="dialog" aria-labelledby="device-note-18-title"><div class="ht-device-note-title" id="device-note-18-title">Guition JC8012P4A1 V3</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>The V2 board with ESP32-P4 v3.2. It gets its own experimental firmware; the V2 image does not start on it. Hardware validation is still pending.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/44">Issue #44</a></p></div>

<div class="ht-device-note" id="device-note-12" popover="auto" role="dialog" aria-labelledby="device-note-12-title"><div class="ht-device-note-title" id="device-note-12-title">Guition JC1060P470C V1</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Touch, brightness, SD, Wi-Fi/MQTT and OTA were confirmed working since v0.6.5. Requires the exact _I_W_Y model.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/8">Issue #8</a></p></div>

<div class="ht-device-note" id="device-note-13" popover="auto" role="dialog" aria-labelledby="device-note-13-title"><div class="ht-device-note-title" id="device-note-13-title">Guition JC1060P470C V2</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Basic operation is confirmed. v0.6.8 includes the orientation correction; full release and OTA confirmation remain pending.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/27">Issue #27</a></p></div>

<div class="ht-device-note" id="device-note-14" popover="auto" role="dialog" aria-labelledby="device-note-14-title"><div class="ht-device-note-title" id="device-note-14-title">Guition ESP32-4848S040C_I</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Hardware-tested by the maintainer. Camera tiles show about 4 frames per second; a brief black screen while saving is expected.</p></div>

<div class="ht-device-note" id="device-note-15" popover="auto" role="dialog" aria-labelledby="device-note-15-title"><div class="ht-device-note-title" id="device-note-15-title">Waveshare LCD-4 Rev 4.0 (S3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>First released in v0.6.10. Display, backlight, touch, Wi-Fi, Web Admin, MQTT and Web OTA are contributor-tested. Rev 4.0 only; Camera tiles and SD access are unavailable.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/pull/35">PR #35</a></p></div>

<div class="ht-device-note" id="device-note-16" popover="auto" role="dialog" aria-labelledby="device-note-16-title"><div class="ht-device-note-title" id="device-note-16-title">Waveshare LCD-4B (S3)</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Initial hardware testing was reported with PR #29. The adapted release profile still needs confirmation. No microSD or Camera tiles.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/26">Issue #26</a><a href="https://github.com/GalusPeres/HomeTiles/pull/29">PR #29</a></p></div>

<div class="ht-device-note" id="device-note-21" popover="auto" role="dialog" aria-labelledby="device-note-21-title"><div class="ht-device-note-title" id="device-note-21-title">Golden Security GS-T3E v2</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Community profile for PCB v2.3, new in v0.9.3. Its pins come from public openHASP and ESPHome configurations; it has its own driver and image, derived from the Guition ESP32-4848S040. Hardware validation by a board owner is still pending. No microSD slot; the relays on GPIO45 and GPIO46 are available under I/O.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/issues/93">Issue #93</a></p></div>

<div class="ht-device-note" id="device-note-17" popover="auto" role="dialog" aria-labelledby="device-note-17-title"><div class="ht-device-note-title" id="device-note-17-title">Guition JC4880P443</div><button type="button" class="ht-device-note-close" aria-label="Close support details">×</button><p>Contributor-tested on JC4880P443C_I_W with a local build: display, touch, rotation, camera tile, Wi-Fi/MQTT, microSD and OTA. Firmware is available in v0.7.0; release-image validation is pending. Portrait 480×800 layout; uses different firmware from the Waveshare LCD-4.3.</p><p class="ht-device-note-links"><a href="https://github.com/GalusPeres/HomeTiles/pull/46">PR #46</a></p></div>

<script src="javascripts/device-status.js?v=3" defer></script>

**New in v0.9.0:** a direct connection without MQTT, pages, an upright mode and a light theme. [Release notes](releases/v0.9.0.md)

Firmware and Bridge are MIT-licensed. [Support HomeTiles](https://buymeacoffee.com/galusperes).
