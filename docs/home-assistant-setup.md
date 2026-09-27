# Home Assistant Setup Guide

Connect your display to Home Assistant, then choose the entities for your dashboard. No YAML editing is required.

!!! note "What you need"
    - Home Assistant 2025.11 or newer
    - A display with [HomeTiles installed](installer.md)

## How It Works

<div class="ht-flow">
  <span class="ht-node">Display</span>
  <span class="ht-link">←&thinsp;MQTT&thinsp;→</span>
  <span class="ht-node">MQTT Broker</span>
  <span class="ht-link">←&thinsp;MQTT&thinsp;→</span>
  <span class="ht-node">Bridge Integration<small>Home Assistant</small></span>
</div>

The Bridge shares Home Assistant data with the display and handles commands from your tiles. Both connect to the same MQTT broker.

## Step 1: Set Up An MQTT Broker { data-toc-label="1. MQTT broker" }

If you already use MQTT in Home Assistant, continue with Step 2. On Home Assistant OS:

1. Open **Settings → Apps → Install app** and install **Mosquitto broker**. Older Home Assistant versions call Apps **Add-ons**.
2. Start it and enable **Start on boot** and **Watchdog**.
3. Under **Settings → People → Users**, add a user such as `hometiles`, with a password. Mosquitto accepts these Home Assistant credentials.
4. Open **Settings → Devices & Services** and confirm the discovered **MQTT** integration. If necessary, add it manually with broker address `core-mosquitto`.

For other installation types, connect Home Assistant to your existing or separately installed broker. See the [official MQTT setup](https://www.home-assistant.io/integrations/mqtt/#setting-up-a-broker).

## Step 2: Install The Bridge Integration { data-toc-label="2. Install bridge" }

<a id="via-hacs-recommended"></a>

1. In **HACS**, open **Custom repositories** from the menu.
2. Add `https://github.com/GalusPeres/HomeTiles-Bridge` as an **Integration**.
3. Download **HomeTiles Bridge**, then restart Home Assistant.

<a id="manual"></a>

Without HACS, copy `custom_components/tab5_lvgl` from the [Bridge repository](https://github.com/GalusPeres/HomeTiles-Bridge) into Home Assistant's `custom_components` directory and restart.

## Step 3: Get The Display Online { #step-4-get-the-display-online data-toc-label="3. Connect WiFi" }

On the display, open **Settings → WiFi**, select your network, and enter its password.

Alternatively, tap **Enable AP**, connect to the hotspot using password `12345678`, and enter your WiFi details in the captive portal.

## Step 4: Pair The Display { #step-5-pair-the-display data-toc-label="4. Pair display" }

A display without saved MQTT credentials announces itself on the network.

1. Under **Settings → Devices & Services** in Home Assistant, open its discovered card.
2. Check the proposed broker address and port, then enter the MQTT username and password from Step 1.
3. Keep the suggested unique base topic and confirm. The Bridge sends these settings to the display.

If you set a [Web Admin password](web-admin.md#web-admin-password) on the display, enter it in the **Panel Web Admin password** field of the card; otherwise leave the field empty. The Bridge uses it only for this pairing and does not store it.

<figure class="ht-screenshot">
<img src="../images/bridge-devices.png" alt="Panels as devices in the bridge integration" width="1328" height="918" loading="lazy">
<figcaption>Displays in the HomeTiles Bridge integration</figcaption>
</figure>

<a id="step-3-add-the-integration"></a>

??? info "Set up without discovery"
    If no discovered card appears, use **Settings → Devices & Services → Add Integration → HomeTiles Bridge** to add the panel manually. Keep the default base topic and HA prefix for your first display.

    Open `http://<display-ip>/` and go to **Settings → MQTT**. Find the IP in the display's WiFi settings.

    - **Host:** your broker's network IP address. With the Mosquitto app, use the Home Assistant machine's IP, not `core-mosquitto`.
    - **Port:** `1883`, unless your broker uses another port.
    - **Username / Password:** the broker credentials from Step 1.
    - **Device topic base / Home Assistant prefix:** use the values from the Bridge entry.

    Select **Save**. The display reconnects to MQTT with the new settings.

## Step 5: Choose What The Display Can See { #step-6-choose-what-the-display-can-see data-toc-label="5. Select entities" }

Open **HomeTiles Bridge → Configure → Entity Configuration** and select the entities you want to use. Selections are shared across all displays.

For Energy tiles, also enable the corresponding electricity, gas, or water category under **Energy Dashboard**. See [Bridge configuration](bridge.md#configuration) for the available options.

## Step 6: Build Your Dashboard { #step-7-build-your-dashboard data-toc-label="6. Create dashboard" }

Open the display's [Web Admin](web-admin.md), add tiles, and assign the selected entities. Changes appear on the display immediately.

Optional GPIO switches, relays, and temperature inputs are configured separately on the panel's [I/O tab](hardware-io.md).

## Multiple Displays

Pair each additional display in the same way. Every panel needs a unique **Device topic base**; the **HA prefix** stays the same. Pairing handles this automatically. For manual configuration, see [Panel Settings](bridge.md#panel-settings).

## Troubleshooting

If pairing fails or entities remain empty, follow the [connection and pairing checks](faq.md#connection-pairing). A previously paired display can announce itself again through **Settings → System → Pairing**.
