// Screensaver Tile Settings > Color in three groups: Icon, Background and
// Text. Background None | Theme | Icon | Custom replaces the "Fill" box on
// screensaver tiles only: None = no card (Tile::ss_fill off), Theme = the
// theme's card, Icon = filled with the icon's color ("fill 20", as on page
// tiles), Custom = an own card color (Tile::ss_card_color, kept with the
// look in /_screensaver/tile_looks.json). Page tiles keep the one color rule
// (color from the icon, per tile only "Fill") untouched.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// ---------------------------------------------------------------------------
// Page tiles: no card color in the icon color record or the tile rules.
assert.doesNotMatch(read('src/tiles/config/tile_icon_colors.h'), /card_of|"card "/);
assert.doesNotMatch(read('src/tiles/config/tile_config.h'), /tileCardColor/);
assert.doesNotMatch(read('src/web/admin/tiles/icon-colors.js'), /'card '|iconColorCardOf/);
const html = read('src/web/server/render/web_admin_html.cpp');
assert.match(html, /if \(!screensaver_mode\) \{\n\s*append_tile_color_from_icon_html\(html, tab_id\);\n\s*\} else \{[\s\S]*?append_screensaver_tile_background_html\(html, tab_id\);/,
  'page tiles keep the Fill box, screensaver tiles get the Background choice');
assert.match(html, /auto append_look_head = \[&\]\(const char\* suffix, const char\* text\) \{\n\s*if \(!screensaver_mode\) return;/,
  'group captions on the screensaver only');
const bgHtml = read('src/web/server/render/tile_icon_colors_html.cpp');
assert.match(fn(bgHtml, 'append_tile_color_from_icon_html'), /appendHtmlEscaped\(html, tr\.tile_fill\);/);
const ssBg = fn(bgHtml, 'append_screensaver_tile_background_html');
for (const marker of ['_tile_icon_fill" data-icon-color="fill-target" hidden>', '_tile_bg_modes', '_tile_ss_card_color" value=']) {
  assert.ok(ssBg.includes(marker), marker);
}

// Panel and server preview draw the card color from the screensaver look.
const look = read('src/ui/screensaver/screensaver_tile_look.h');
assert.match(look, /inline bool has_card_color\(const Tile& tile\) \{ return \(tile\.ss_card_color & kColorSet\) != 0; \}/);
const panel = read('src/ui/screensaver/image_screensaver.cpp');
assert.match(fn(panel, 'apply_slot_look'),
  /if \(screensaver_tile_look::has_card_color\(tile\)\) \{[\s\S]*?lv_obj_set_style_bg_color\(card, lv_color_hex\(rgb\), LV_PART_MAIN \| LV_STATE_DEFAULT\);/);
assert.match(fn(panel, 'build_slot_tile'), /if \(opacity < LV_OPA_COVER \|\| !tile\.ss_fill \|\| screensaver_tile_look::has_card_color\(tile\)\) \{\n\s*tile_icon_disc::refresh_fills/,
  'the circles follow the card color');
assert.match(html, /const bool own_card = screensaver_mode && screensaver_tile_look::has_card_color\(tile\);/);
const config = read('src/ui/screensaver/screensaver_config.cpp');
assert.match(config, /if \(card\.length\(\)\) item\["card"\] = card;/);
assert.match(config,
  /tile\.ss_card_color = screensaver_tile_look::parse_color\(String\(item\["card"\] \| ""\)\);/);

// ---------------------------------------------------------------------------
// Translations in every language.
const i18nHeader = read('src/core/i18n/i18n.h');
const i18n = read('src/core/i18n/i18n.cpp');
const keys = ['tile_look_group_background', 'tile_look_group_text', 'tile_bg_color',
  'tile_bg_none', 'tile_bg_plain', 'tile_bg_icon'];
const order = [...i18nHeader.matchAll(/const char\* (\w+);/g)].map(m => m[1]);
assert.deepEqual(order.slice(order.indexOf(keys[0]), order.indexOf(keys[0]) + keys.length), keys);
const expected = {
  De: ['Hintergrund', 'Text', 'Hintergrundfarbe', 'Keiner', 'Design', 'Icon'],
  En: ['Background', 'Text', 'Background color', 'None', 'Theme', 'Icon'],
  Fr: ['Arrière-plan', 'Texte', 'Couleur de fond', 'Aucun', 'Thème', 'Icône'],
  Pl: ['Tło', 'Tekst', 'Kolor tła', 'Brak', 'Motyw', 'Ikona'],
};
for (const [lang, texts] of Object.entries(expected)) {
  const table = i18n.slice(i18n.indexOf(`static const Strings kStrings${lang} = {`));
  const start = table.indexOf(`"${texts[0]}",`);
  assert.ok(start >= 0, lang);
  const entries = [...table.slice(start).matchAll(/^\s*"((?:[^"\\]|\\.)*)",/gm)].slice(0, texts.length).map(m => m[1]);
  assert.deepEqual(entries, texts, lang);
}

// ---------------------------------------------------------------------------
// Headless Chrome: the real editor functions on the screensaver's Color
// section and on a page tile's.
const iconSections = tab => `<div id="${tab}_tile_icon_color_fields" class="tile-icon-color-fields hidden" data-tab="${tab}">
<div class="icon-color-section hidden" id="${tab}_tile_icon_source_section"><input type="hidden" id="${tab}_tile_icon_rules_on" value="0">
<div class="icon-color-rules-body hidden" id="${tab}_tile_icon_rules_body"><input type="hidden" id="${tab}_tile_icon_source_kind" value="self">
<div id="${tab}_tile_icon_source_kinds"></div><input type="hidden" id="${tab}_tile_icon_source"><input type="hidden" id="${tab}_tile_icon_source_mode" value="rules">
<div id="${tab}_tile_icon_source_modes"></div></div></div>
<div class="icon-color-section hidden" id="${tab}_tile_icon_bar_section"><input type="hidden" id="${tab}_tile_icon_bar" value=""></div>
<div class="icon-color-section hidden" id="${tab}_tile_icon_state_section"></div>
<div class="icon-color-section hidden" id="${tab}_tile_icon_binary_section"></div></div>`;
const common = tab => `<select id="${tab}_tile_type"><option value="2">Scene</option><option value="10">Text</option></select>
<input id="${tab}_tile_title"><input id="${tab}_tile_icon"><input id="${tab}_tile_color" type="color" value="#2A2A2A">
<div id="${tab}_tile_color_modes"><button data-tile-color-mode="global"></button></div><div id="${tab}_tile_color_row"></div>
<div class="tile-icon-color-fields tile-icon-color-fixed hidden" id="${tab}_tile_icon_color_fixed" data-tab="${tab}">
<div id="${tab}_tile_icon_color_modes"></div><input type="checkbox" id="${tab}_tile_icon_cover" hidden>
<div class="tile-color-row no-reset" id="${tab}_tile_icon_color_row"><input type="color" id="${tab}_tile_icon_color" value="#FFFFFF" data-unset="1" data-icon-color="color"></div></div>`;
const fixture = `<div class="tile-grid screensaver-tile-grid" id="screensaverGrid"><div class="tile" id="screensaver-tile-0" data-index="0"></div></div>
<div class="tile-settings" style="width:400px">${common('screensaver')}
<div class="tile-look-head" id="screensaver_tile_look_icon_head">Icon</div>
<input type="checkbox" id="screensaver_tile_ss_icon" checked>
<div class="tile-look-head" id="screensaver_tile_look_bg_head">Background</div>
<div class="tile-icon-color-fields icon-color-fill hidden" id="screensaver_tile_icon_fill_row" data-tab="screensaver">
<input type="checkbox" id="screensaver_tile_icon_fill" data-icon-color="fill-target" hidden>
<div class="icon-color-segmented choice-row tile-bg-modes" id="screensaver_tile_bg_modes">${['none', 'plain', 'icon', 'custom'].map(m =>
  `<button type="button" id="bg_${m}" data-tile-bg-mode="${m}" onclick="setTileBgMode('screensaver', '${m}')">${m}</button>`).join('')}</div>
<div class="tile-color-row no-reset hidden" id="screensaver_tile_ss_card_color_row"><input type="color" id="screensaver_tile_ss_card_color" value="#3A4A5A" data-set="0"></div></div>
<div class="screensaver-tile-look"><input type="checkbox" id="screensaver_tile_ss_fill" checked hidden>
<label id="screensaver_tile_ss_opacity_row"><input type="range" id="screensaver_tile_ss_opacity" min="0" max="255" value="255"><output id="screensaver_tile_ss_opacity_value"></output></label></div>
<div class="tile-look-head" id="screensaver_tile_look_text_head">Text</div>
<input type="checkbox" id="screensaver_tile_ss_text_color_on" hidden>
<div id="screensaver_tile_ss_text_modes"><button id="text_auto" data-ss-text-mode="auto" onclick="setScreensaverTextColorMode('screensaver', 'auto')">A</button><button id="text_custom" data-ss-text-mode="custom" onclick="setScreensaverTextColorMode('screensaver', 'custom')">C</button></div>
<div class="tile-color-row no-reset hidden" id="screensaver_tile_ss_text_color_row"><input type="color" id="screensaver_tile_ss_text_color" value="#FFFFFF"></div>
${iconSections('screensaver')}</div>
<div class="tile-settings" style="width:400px">${common('t')}
<div class="tile-icon-color-fields icon-color-fill hidden" id="t_tile_icon_fill_row" data-tab="t">
<label class="inline-checkbox"><input type="checkbox" id="t_tile_icon_fill" data-icon-color="fill-target"> Fill</label></div>
${iconSections('t')}</div>`;
const page = `<!doctype html><html lang="en"><head><style>${read('src/web/assets/admin.css')}</style></head><body>
${fixture}<pre id="result"></pre><script>
const errors=[];window.addEventListener('error',e=>errors.push(String(e.message)));
const nativeListen=document.addEventListener.bind(document);document.addEventListener=(name,...args)=>{if(name!=='DOMContentLoaded')nativeListen(name,...args);};
const APP_I18N={},CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={0:{},2:{css:'scene',preview:'scene',defaultBg:'#2A2A2A'},10:{css:'text',preview:'text',defaultBg:'#2A2A2A'}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=255,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
window.fetch=async()=>({ok:true,json:async()=>({success:true})});
${inlineScriptSafe(readAdminDeliverySource())}
try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const calls=[];updateTilePreview=t=>calls.push('preview');updateDraft=t=>calls.push('draft');scheduleAutoSave=t=>calls.push('autosave');
 const $=id=>document.getElementById(id);
 const hidden=id=>$(id).classList.contains('hidden');
 const pressed=()=>[...document.querySelectorAll('#screensaver_tile_bg_modes [data-tile-bg-mode]')].filter(b=>b.getAttribute('aria-pressed')==='true').map(b=>b.dataset.tileBgMode).join();
 const S='screensaver';
 const load=(record,look)=>{loadIconColorFields(S,{icon_colors:record});loadScreensaverLookFields(S,look||{});};
 $('screensaver_tile_type').value='2';
 // The stored fields pick the mode.
 load('v2\\n\\nfill 20',{ss_fill:1});
 check(tileBgMode(S)==='icon'&&pressed()==='icon','Fill on -> Icon');
 load('',{ss_fill:1});
 check(tileBgMode(S)==='plain'&&pressed()==='plain','Fill off -> Theme');
 load('v2\\n\\nfill 20',{ss_fill:0});
 check(tileBgMode(S)==='none'&&pressed()==='none','no fill -> None');
 check(hidden('screensaver_tile_ss_opacity_row'),'None hides the opacity');
 load('',{ss_fill:1,ss_card_color:'#336699'});
 check(tileBgMode(S)==='custom'&&!hidden('screensaver_tile_ss_card_color_row')&&$('screensaver_tile_ss_card_color').value==='#336699','a card color -> Custom with its color');
 check(!hidden('screensaver_tile_ss_opacity_row'),'a card shows the opacity');
 // Choosing a mode writes the underlying fields and takes the live path.
 load('',{ss_fill:1});
 let picker=0;$('screensaver_tile_ss_card_color').showPicker=()=>{picker++;};
 calls.length=0;$('bg_custom').click();
 check(picker===1,'the first Custom opens the card color picker');
 check(calls.join()==='preview,draft,autosave','preview, draft and autosave');
 $('screensaver_tile_ss_card_color').value='#123456';
 check(collectScreensaverLookFields(S,'2').ss_card_color==='#123456','Custom stores the card color in the look');
 check(collectIconColorRecord(S)==='','the icon color record stays without a card');
 $('bg_icon').click();
 check(collectIconColorRecord(S)==='v2\\n\\nfill 20'&&$('screensaver_tile_icon_fill').checked,'Icon stores the fill');
 check(collectScreensaverLookFields(S,'2').ss_card_color==='','Icon drops the card color');
 $('bg_none').click();
 check(!$('screensaver_tile_ss_fill').checked&&collectScreensaverLookFields(S,'2').ss_fill==='0','None clears the card');
 check(collectIconColorRecord(S)==='','None keeps no fill');
 $('bg_plain').click();
 check($('screensaver_tile_ss_fill').checked&&collectIconColorRecord(S)===''&&pressed()==='plain','Theme: the theme card');
 // Clock/Text keep their own color: no Background choice, a card is dropped.
 $('screensaver_tile_type').value='10';
 $('screensaver_tile_ss_card_color').dataset.set='1';
 syncTileColorMode(S);
 check(hidden('screensaver_tile_icon_fill_row')&&$('screensaver_tile_ss_card_color').dataset.set==='0','Text keeps its own color choice');
 check(screensaverLookCardColor({ss_card_color:'#123456'},'10')==='','Text never reads a card');
 $('screensaver_tile_type').value='2';syncTileColorMode(S);
 // Text color: Automatic | Custom.
 $('screensaver_tile_ss_text_color').showPicker=()=>{};
 $('text_custom').click();
 check($('screensaver_tile_ss_text_color_on').checked&&!hidden('screensaver_tile_ss_text_color_row'),'Custom text color shows its picker');
 check(collectScreensaverLookFields(S,'2').ss_text_color==='#FFFFFF','and is saved');
 $('text_auto').click();
 check(!$('screensaver_tile_ss_text_color_on').checked&&hidden('screensaver_tile_ss_text_color_row')&&collectScreensaverLookFields(S,'2').ss_text_color==='','Automatic');
 // Cached grid preview: the card color is the tile's background.
 currentTileTab=S;
 renderTileFromData(S,0,{type:2,title:'Movie',icon_colors:'',bg_color:0,ss_card_color:'#336699'},null);
 const bg=getComputedStyle($('screensaver-tile-0')).backgroundColor;
 check(bg==='rgb(51, 102, 153)','cached preview card color: '+bg);
 // A page tile: the Fill box, no Background choice, no look.
 $('t_tile_type').value='2';
 loadIconColorFields('t',{icon_colors:'v2\\n\\nfill 20'});
 syncTileColorMode('t');
 check($('t_tile_icon_fill').checked&&collectIconColorRecord('t')==='v2\\n\\nfill 20','a page tile keeps Fill');
 check(Object.keys(collectScreensaverLookFields('t','2')).length===0,'a page tile has no screensaver look');
 check(errors.length===0,'no script errors: '+errors.join('; '));
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}
</script></body></html>`;
try {
  if (runDomHarness({label: 'Screensaver tile background', html: page, tmpPrefix: 'hometiles-ss-bg-'})) {
    console.log('Screensaver tile background: modes, card color in the look, previews and translations; page tiles keep Fill');
  }
} catch (error) { throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 600)); }
