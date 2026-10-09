# Screensaver

Create a separate layout with a clock, tiles, and an optional image slideshow or a picture from Home Assistant in the Web Admin's **Screensaver** tab.

<figure class="ht-screenshot">
<img src="../images/8in-screensaver.png" alt="Screensaver on the Waveshare 8 inch display" width="1308" height="828" loading="lazy">
<figcaption>Screensaver with clock and sensor tiles</figcaption>
</figure>

## Open It Manually or Automatically { data-toc-label="Start screensaver" }

- Tap a **Clock** tile to open it immediately.
- Set an inactivity timeout under **Settings → Display → Screensaver**. **Never** disables automatic activation.
- Use **Screensaver Brightness** for separate screensaver brightness; the display previews the level while you drag.
- Tap free background space to return to the dashboard.

<figure class="ht-screenshot">
<img src="../images/8in-display-popup.png" alt="Display settings with screensaver timeout" width="1272" height="792" loading="lazy">
<figcaption>Display and screensaver settings</figcaption>
</figure>

## Prepare the microSD Card { data-toc-label="Prepare images" }

A slideshow needs microSD support. The Waveshare S3 LCD-4 Rev 4.0 and S3 LCD-4B profiles cannot use SD images. The clock and tiles work on a black background without a card. A [picture from Home Assistant](#picture-from-home-assistant) needs no card.

1. Format the card as FAT32 and insert it.
2. Create an `images` folder in the card's root (`/images`).
3. Copy your JPEGs there, or upload them with the Web Admin file manager.
4. Open the **Screensaver** tab. New images are added to the list, and deleted files disappear from it.

### Image Requirements { data-toc-label="Image format" }

<a id="reliable-export-recipe"></a>

- Export **baseline (non-progressive) JPEG**, with a `.jpg` or `.jpeg` extension.
- Use **RGB/sRGB**, not CMYK.
- Resize to your display's resolution in the [device list](index.md#device-support). For a shared image set, keep the **longest edge at 1920 pixels or less**.
- Use quality 80–90 and keep each file below **8 MB**.

If an image stays black, re-export it at the display's native size with these settings. A large camera photo or progressive JPEG may fail even if the filename ends in `.jpg`.

## Configure the Slideshow { data-toc-label="Slideshow settings" }

In the Web Admin's **Screensaver** tab, click the background.

<figure class="ht-screenshot">
<img src="../images/web-admin-screensaver.png" alt="Screensaver editor in the web admin" width="1450" height="895" loading="lazy">
<figcaption>Screensaver editor</figcaption>
</figure>

| Setting | What it does |
| --- | --- |
| **Use images** | Enable the slideshow instead of a black background |
| **Shuffle** | Choose images randomly |
| **Tile shadows / Tile borders** | Style the overlay tiles |
| **Image checkboxes** | Include or exclude files |
| **Arrow buttons** | Change the image order |
| **Duration** | Display time for every image |
| **Zoom** | Enlarge the selected image |
| **Focus X / Focus Y** | Move its crop horizontally or vertically |

Changes save automatically.

## Picture from Home Assistant { data-toc-label="Home Assistant picture" }

Instead of the slideshow, the screensaver can show the picture of a Home Assistant `image` or `camera` entity, for example a weather webcam.

1. Connect the panel to the Bridge directly (pairing on the panel); panels on MQTT keep the slideshow.
2. Release the entity in the Bridge options under **Images (screensaver)**. A camera released under **Cameras** works too.
3. In the **Screensaver** tab, click the background, set **Type** to **Picture from Home Assistant**, keep **Use images** on, and choose the entity.

A new picture of an image entity appears at once. A camera sends a new still image at the interval set under **Refresh (s)**, 3 to 60 seconds, and only while the screensaver is visible. **Scaling** sets how the picture uses the screen:

| Scaling | What it does |
| --- | --- |
| **Fill** | Covers the whole screen; what stands out is cut off |
| **Fit** | The whole picture as large as possible, black bars where needed |
| **Original** | A smaller picture at its own size in the middle; a larger one like **Fit** |

QR codes and maps are enlarged with sharp edges. Upright displays get the picture in their upright size. Switch back to **Slideshow (SD card)** at any time; the slideshow settings stay stored.

## Position the Clock

Select the clock to change its time/date formats, font sizes, alignment, and shadow. Drag it anywhere and resize its text area; it is independent of the tile grid.

## Add Tiles

The bottom two rows support Sensor, Binary Sensor, Energy, Switch, Scene, and Media tiles. Select a slot, choose its type, and move or resize it as on the dashboard. Adjust **Color** and **Opacity** together.

States stay live. Popups do not open in screensaver mode. Local I/O switches and temperatures remain available when Home Assistant is offline.

Media tiles need at least 2×2 cells. Cover art can slow entry and slide changes; use fewer Media tiles if the slideshow feels slow.

## Backup and Restore

[Web Admin Export](web-admin.md#import-export) includes the screensaver layout and settings. Image files stay on the microSD card and must be copied separately.
