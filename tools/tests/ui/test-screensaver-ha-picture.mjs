// Issue #69: the screensaver shows the Bridge's picture of a Home Assistant
// image or camera entity instead of the SD slideshow (docs-dev/images.md). A
// switch in the Web Admin picks the source. An image entity stays subscribed,
// so its picture is decoded ahead; a camera only while the screensaver shows
// it on an awake display, since the Bridge loads a new still for a subscribed
// camera at the chosen interval (3..60 s). The Bridge places the picture as
// chosen: filled, whole with black bars, or 1:1 when smaller.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const screensaver = readRepoFile('src/ui/screensaver/image_screensaver.cpp');
const section = (text, start, end) => {
  const from = text.indexOf(start);
  const to = text.indexOf(end, from);
  assert.ok(from >= 0 && to > from, `${start} .. ${end}`);
  return text.slice(from, to);
};

// Opening: the picture instead of the slideshow; no SD card needed.
const show = section(screensaver, 'void show_image_screensaver()', 'void hide_image_screensaver()');
assert.match(show, /if \(config\.use_wallpapers && screensaver_uses_ha_picture\(config\)\) \{\s*wallpaper_visible = apply_ha_picture\(st\);\s*\} else \{\s*wallpaper_visible = apply_wallpaper\(st, first_enabled_wallpaper\(\), true\);\s*\}/);

// The picture is decoded where bridge_images keeps it and shown like a slide.
const apply = section(screensaver, 'bool apply_ha_picture(ScreensaverState* st)', 'void refresh_slot_values(');
assert.match(apply, /st->active_wallpaper = -1;/, 'no slide replaces the picture');
assert.doesNotMatch(apply, /read_wallpaper_file|Device::sd/, 'the picture never touches the SD card');
assert.match(apply, /get_or_decode_cached\(\s*source, grid_layout::screen_w\(\), grid_layout::screen_h\(\), st->image, picture\);/);
assert.match(apply, /present_picture\(st, dsc, source\.file_name, cache_hit\);/);
const cached = section(screensaver, 'lv_image_dsc_t* get_or_decode_cached(', 'bool ha_picture_source(');
assert.match(cached, /picture \? decode_jpeg_to_size\(name\.c_str\(\), picture->jpeg, picture->length, false,/,
  'the Bridge picture is decoded in place and stays owned by bridge_images');
const source = section(screensaver, 'bool ha_picture_source(', 'bool is_wallpaper_file(');
assert.match(source, /out\.file_name = prefix \+ picture->key \+ "\/" \+ String\(picture->fit\);/,
  'a new picture key or placement decodes again');
assert.match(source, /if \(g_cache_dsc && g_cache_name\.startsWith\(prefix\)\) \{\s*out\.file_name = g_cache_name;\s*return true;\s*\}/,
  'without a stored picture the decoded one of the same entity stays');
const decode = section(screensaver, 'lv_image_dsc_t* decode_jpeg_to_size(', 'lv_image_dsc_t* decode_wallpaper_to_size(');
assert.match(decode, /if \(owned\) free\(const_cast<uint8_t\*>\(data\)\);/, 'only an SD file is freed by the decoder');
assert.match(screensaver, /return decode_jpeg_to_size\(file_name\.c_str\(\), file, len, true,/);

// A new picture: the open screensaver shows it from its LVGL timer once
// touches settled; a hidden one decodes it ahead.
const arrived = section(screensaver, 'void image_screensaver_picture_arrived(', 'void image_screensaver_tiles_changed()');
assert.match(arrived, /!config\.picture_entity\.equalsIgnoreCase\(entity_id\)/);
assert.match(arrived, /if \(g_state\) \{\s*g_ha_picture_arrived = true;\s*if \(g_state->timer\) lv_timer_ready\(g_state->timer\);\s*return;\s*\}/);
assert.match(arrived, /g_preload_ha_picture = true;\s*schedule_preload\(1000\);/);
// A picture not decoded (PSRAM taken at that moment, e.g. by the panel's own
// camera) is tried again every 3 s, at most 20 times per picture.
assert.match(screensaver, /constexpr uint32_t kHaPictureRetryMs = 3000;\s*constexpr uint8_t kHaPictureRetries = 20;/);
assert.match(apply, /if \(!dsc\) \{[\s\S]*?if \(picture\) \{\s*if \(g_ha_retry_name != source\.file_name\) \{\s*g_ha_retry_name = source\.file_name;\s*g_ha_retries_left = kHaPictureRetries;\s*\}\s*if \(g_ha_retries_left\) \{\s*--g_ha_retries_left;\s*g_ha_retry_at_ms = millis\(\) \+ kHaPictureRetryMs;/);
assert.match(apply, /g_ha_retry_at_ms = 0;\s*st->active_wallpaper_name = source\.file_name;/, 'a decoded picture ends the retries');
const timer = section(screensaver, 'void global_screensaver_timer_cb(', 'void on_global_screensaver_clicked(');
assert.match(timer, /if \(g_ha_retry_at_ms && static_cast<int32_t>\(now - g_ha_retry_at_ms\) >= 0\) \{\s*g_ha_retry_at_ms = 0;\s*g_ha_picture_arrived = true;\s*\}/);
assert.match(timer, /if \(g_ha_picture_arrived\) \{[\s\S]*?if \(since_activity >= kInteractionSettleBeforeSlideMs\) \{\s*g_ha_picture_arrived = false;[\s\S]*?apply_ha_picture\(st\);/);
const preload = section(screensaver, 'void global_preload_timer_cb(', 'void schedule_preload(');
assert.match(preload, /ha_picture_source\(source, picture\) && picture\) \{\s*get_or_decode_cached\(source, grid_layout::screen_w\(\), grid_layout::screen_h\(\),\s*nullptr, picture\);/);
const refresh = section(screensaver, 'void refresh_live_background_and_clock(', 'void global_screensaver_timer_cb(');
assert.match(refresh, /if \(screensaver_uses_ha_picture\(config\)\) \{\s*if \(!apply_ha_picture\(st\)\) clear_live_wallpaper\(st\);/,
  'a source change in the Web Admin applies to the open screensaver');

// The camera's still follows visibility and display sleep, every loop pass.
const sync = section(screensaver, 'void sync_live_picture()', '}  // namespace');
assert.match(sync, /const bool live = g_state && !powerManager\.isInSleep\(\) && config\.use_wallpapers &&\s*screensaver_uses_ha_picture\(config\) &&\s*config\.picture_entity\.startsWith\("camera\."\);/);
assert.match(sync, /const uint8_t fit = live \? config\.picture_fit : 0;\s*const uint8_t every = live \? config\.picture_every : 0;/);
assert.match(sync, /if \(g_live_picture_entity == entity && g_live_picture_fit == fit &&\s*g_live_picture_every == every\) \{\s*return;\s*\}[\s\S]*?mqttSetLivePicture\(entity, fit, every\);/,
  'a new camera, placement or interval subscribes again');
assert.match(screensaver, /void service_image_screensaver_auto\(uint32_t last_activity_ms\) \{\s*sync_live_picture\(\);\s*if \(g_state \|\| powerManager\.isInSleep\(\)\) return;/);
const loop = readRepoFile('HomeTiles.ino');
assert.ok(loop.indexOf('service_image_screensaver_auto(displayManager.getLastActivityTime());') <
  loop.indexOf('// --- SLEEP ---'), 'the sync also runs while the display sleeps');

// Subscriptions: an image entity with the routes, a camera on demand; both in
// the screen's size and only over the link.
const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
assert.match(handlers, /if \(networkManager\.linkConfigured\(\) && screensaver\.use_wallpapers &&\s*screensaver_uses_ha_picture\(screensaver\) &&\s*screensaver\.picture_entity\.startsWith\("image\."\)\) \{\s*add_route\(screensaver\.picture_entity, -1, bridge_images::screenSuffix\(screensaver\.picture_fit, 0\)\);/);
const live = section(handlers, 'void mqttSetLivePicture(', 'static void rebuildDynamicRoutes(');
assert.match(live, /topic = buildHaStatestreamTopic\(String\(entity_id\), bridge_images::screenSuffix\(fit, every\)\);/);
assert.match(live, /if \(topic == g_live_picture_topic\) return;/);
assert.match(live, /if \(networkManager\.isMqttConnected\(\) && networkManager\.linkConfigured\(\)\) \{\s*if \(g_live_picture_topic\.length\(\)\) \{\s*networkManager\.mqttEnqueueUnsubscribe\(g_live_picture_topic\.c_str\(\)\);\s*\}\s*if \(topic\.length\(\)\) networkManager\.mqttEnqueueSubscribe\(topic\.c_str\(\)\);/);
const subscribe = section(handlers, 'void mqttSubscribeTopics()', '// ========== Publish home snapshot');
assert.match(subscribe, /if \(g_live_picture_topic\.length\(\) && networkManager\.linkConfigured\(\)\) \{\s*networkManager\.mqttEnqueueSubscribe\(g_live_picture_topic\.c_str\(\)\);/,
  'a reconnect subscribes the shown camera again');

// Saving a new source subscribes and declares at once.
const save = readRepoFile('src/web/server/handlers/web_admin_screensaver.cpp');
assert.ok(save.indexOf('const String old_entity') < save.indexOf('screensaverConfig.replaceFromJson('),
  'the old source is copied before the save replaces it');
assert.match(save, /if \(had_picture != has_picture \|\|\s*\(has_picture && \(old_entity != after\.picture_entity \|\| old_fit != after\.picture_fit\)\)\) \{\s*mqttRequestDynamicSlotsReload\(1000\);\s*\}/);
const search = readRepoFile('src/network/bridge/entity_search.cpp');
assert.match(search, /if \(screensaver_config\.use_wallpapers && screensaver_uses_ha_picture\(screensaver_config\)\) \{\s*declaration\.add\("images", screensaver_config\.picture_entity\.c_str\(\)\);/,
  'the panel tells the Bridge which picture it uses');

// Stored configuration: the source and a validated entity.
const configHeader = readRepoFile('src/ui/screensaver/screensaver_config.h');
assert.match(configHeader, /return \(entity\.startsWith\("image\."\) && entity\.length\(\) > 6\) \|\|\s*\(entity\.startsWith\("camera\."\) && entity\.length\(\) > 7\);/);
const config = readRepoFile('src/ui/screensaver/screensaver_config.cpp');
assert.match(config, /loaded\.picture_from_ha = strcmp\(doc\["picture_source"\] \| "sd", "ha"\) == 0;/);
assert.match(config, /if \(!screensaver_picture_entity_valid\(loaded\.picture_entity\)\) loaded\.picture_entity = "";/);
assert.match(config, /doc\["picture_source"\] = data_\.picture_from_ha \? "ha" : "sd";\s*doc\["picture_entity"\] = data_\.picture_entity;/);
assert.match(config, /loaded\.picture_fit = strcmp\(fit, "fit"\) == 0 \? 1 : strcmp\(fit, "original"\) == 0 \? 2 : 0;/);
assert.match(config, /clamp_u16\(doc\["picture_every"\]\.as<int>\(\), 3, 60\)\)\s*: 10;/, 'older files keep 10 s');
assert.match(config, /doc\["picture_fit"\] = data_\.picture_fit == 1 \? "fit" : data_\.picture_fit == 2 \? "original" : "fill";\s*doc\["picture_every"\] = data_\.picture_every;/);

// The entity picker offers released images and cameras.
const options = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(options, /std::vector<String> picture_ids = parseSensorList\(ha\.images_text\);\s*for \(const auto& id : parseSensorList\(ha\.cameras_text\)\) addUnique\(picture_ids, id\);\s*appendList\("images", picture_ids\);/);
assert.match(readRepoFile('src/web/admin/tiles/entity-picker.js'), /'cameras', 'images', 'locks'/);
assert.match(readRepoFile('src/network/bridge/ha_bridge_config.cpp'), /std::make_pair\("\\"images\\"", &merged\.images_text\)/);

// Web Admin markup: the source switch and picker, every label translated.
const html = readRepoFile('src/web/server/render/web_admin_html.cpp');
assert.match(html, /<select id="screensaverPictureSource"><option value="sd">\)html";\s*appendHtmlEscaped\(html, tr\.screensaver_source_sd\);\s*html \+= R"html\(<\/option><option value="ha">\)html";\s*appendHtmlEscaped\(html, tr\.screensaver_source_ha\);/);
assert.match(html, /appendEntityPickerField\(html, "screensaver", "picture_entity", tr\.screensaver_picture_entity, "images"\);/);
assert.match(html, /appendHtmlEscaped\(html, tr\.screensaver_picture_hint\);/);
assert.match(html, /<select id="screensaverPictureFit"><option value="fill">\)html";\s*appendHtmlEscaped\(html, tr\.screensaver_fit_fill\);[\s\S]*?<option value="fit">\)html";\s*appendHtmlEscaped\(html, tr\.screensaver_fit_contain\);[\s\S]*?<option value="original">\)html";\s*appendHtmlEscaped\(html, tr\.screensaver_fit_original\);/);
assert.match(html, /<div id="screensaverPictureEveryRow" class="screensaver-fixed-type hidden">[\s\S]*?appendHtmlEscaped\(html, tr\.screensaver_picture_every\);[\s\S]*?<input id="screensaverPictureEvery" type="number" min="3" max="60" step="1" value="10">/);
const i18nHeader = readRepoFile('src/core/i18n/i18n.h');
assert.match(i18nHeader, /const char\* screensaver_source_sd;\s*const char\* screensaver_source_ha;\s*const char\* screensaver_picture_entity;\s*const char\* screensaver_picture_hint;\s*const char\* screensaver_picture_fit;\s*const char\* screensaver_fit_fill;\s*const char\* screensaver_fit_contain;\s*const char\* screensaver_fit_original;\s*const char\* screensaver_picture_every;/);
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
const escape = text => text.replace(/[()]/g, '\\$&');
for (const words of [
  ['Diashow (SD-Karte)', 'Bild aus Home Assistant', 'Bild oder Kamera', null, 'Anpassung', 'Füllen', 'Einpassen', 'Original', 'Aktualisieren (s)'],
  ['Slideshow (SD card)', 'Picture from Home Assistant', 'Image or camera', null, 'Scaling', 'Fill', 'Fit', 'Original', 'Refresh (s)'],
  ['Diaporama (carte SD)', 'Image de Home Assistant', 'Image ou caméra', null, 'Ajustement', 'Remplir', 'Ajuster', 'Original', 'Actualiser (s)'],
  ['Pokaz slajdów (karta SD)', 'Obraz z Home Assistant', 'Obraz lub kamera', null, 'Dopasowanie', 'Wypełnij', 'Dopasuj', 'Oryginał', 'Odświeżanie (s)']]) {
  const pattern = words.map(word => word === null ? '"[^"]+"' : `"${escape(word)}"`).join(',\\s*');
  assert.match(i18n, new RegExp(pattern), words.filter(Boolean).join(' / '));
}

// Browser editor: the source and entity load, default to the slideshow and
// reach the saved payload.
const names = ['ssClamp', 'ssNearestClockFont', 'ssClockAlignment', 'ssNormalizeLoaded', 'ssPayload',
  'ssCurrentWallpaper'];
const context = vm.createContext({
  structuredClone, JSON, Math, Number, String, Array, Set, Map,
  screensaverDraft: null, screensaverWallpaperIndex: -1,
  screensaverTimeFontSizes: [20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 96],
  screensaverDateFontSizes: [20, 24, 28, 32, 40, 48, 56, 64, 72],
  SCREENSAVER_TILE_DEFAULT_OPACITY: 217,
});
vm.runInContext(names.map(extractDeliveredFunction).join('\n'), context);
const legacy = context.ssNormalizeLoaded({success: true, wallpapers: []});
assert.equal(legacy.picture_source, 'sd', 'older panels keep the slideshow');
assert.equal(legacy.picture_entity, '');
const loaded = context.ssNormalizeLoaded({success: true, wallpapers: [], picture_source: 'ha',
  picture_entity: 'camera.haustuer'});
assert.equal(loaded.picture_source, 'ha');
const payload = context.ssPayload(loaded);
assert.equal(payload.picture_source, 'ha');
assert.equal(payload.picture_entity, 'camera.haustuer');
assert.equal(context.ssPayload({...loaded, picture_source: 'x'}).picture_source, 'sd');
assert.equal(legacy.picture_fit, 'fill', 'older panels keep filling');
assert.equal(legacy.picture_every, 10);
const placed = context.ssNormalizeLoaded({success: true, wallpapers: [], picture_fit: 'original', picture_every: 99});
assert.equal(placed.picture_fit, 'original');
assert.equal(placed.picture_every, 60, 'the interval stays within 3..60 s');
assert.equal(context.ssNormalizeLoaded({success: true, wallpapers: [], picture_every: 1}).picture_every, 3);
assert.equal(context.ssPayload(placed).picture_fit, 'original');
assert.equal(context.ssPayload({...placed, picture_fit: 'stretch'}).picture_fit, 'fill');
assert.equal(context.ssPayload(placed).picture_every, 60);
const editor = readRepoFile('src/web/admin/screensaver/editor.js');
assert.match(editor, /bind\('screensaverPictureSource', 'change', el => \{\s*screensaverDraft\.picture_source = el\.value === 'ha' \? 'ha' : 'sd';\s*\}\);/);
assert.match(editor, /bind\('screensaver_picture_entity', 'change', el => \{ screensaverDraft\.picture_entity = el\.value \|\| ''; \}\);/);
assert.match(editor, /const wallpaper = fromHa \? null : ssCurrentWallpaper\(\);/, 'the preview shows no SD image for the HA source');
assert.match(editor, /document\.getElementById\('screensaverPictureEveryRow'\)\?\.classList\.toggle\('hidden',\s*!String\(d\.picture_entity \|\| ''\)\.startsWith\('camera\.'\)\);/,
  'only a camera shows its interval');
assert.match(editor, /bind\('screensaverPictureFit', 'change', el => \{\s*screensaverDraft\.picture_fit = \['fit', 'original'\]\.includes\(el\.value\) \? el\.value : 'fill';\s*\}\);/);
assert.match(editor, /bind\('screensaverPictureEvery', 'change', el => \{\s*screensaverDraft\.picture_every = Math\.round\(ssClamp\(Number\(el\.value\) \|\| 10, 3, 60\)\);\s*\}\);/);

console.log('Screensaver picture from Home Assistant: source switch, placement, interval, image ahead, camera only while shown, retried');
