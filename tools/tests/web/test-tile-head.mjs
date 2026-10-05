// The tile head: the heading with Type beside it, Title and Icon side by side,
// then the entity, all outside the scrolling body. The title has at most two
// lines; the icon picker offers Automatic (the entity's icon), No icon and
// every icon the panel can draw (/api/mdi_icons, streamed from the flash
// table). Covers the server contract, the translations in every language and
// the real Admin bundle in headless Chrome.
import assert from 'node:assert/strict';

import {inlineScriptSafe, readAdminDeliverySource, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// ---- Server ----
const page = read('src/web/server/render/web_admin_html.cpp');
const head = page.slice(page.indexOf('<div class="tile-settings-head">\n              <div class="tile-head-top">'),
  page.indexOf('append_tile_icon_color_fixed_html(html, tab_id);'));
assert.ok(head.length > 0, 'The tile head markup exists');
for (const marker of ['<div class="tile-head-top"><h3>', '_tile_type" aria-label=")html";',
  '<div class="tile-head-row"><div><label for=")html";', '<textarea rows="1" class="tile-title-input" spellcheck="false"',
  '_tile_icon" data-tile-icon><button type="button" class="tile-icon-picker"', '_tile_entity_slot"></div>',
  '<div class="tile-settings-body">']) {
  assert.ok(head.includes(marker), `Head markup: ${marker}`);
}
assert.ok(head.indexOf('tile-head-top') < head.indexOf('tile-entity-slot') &&
  head.indexOf('tile-entity-slot') < head.indexOf('tile-head-row') &&
  head.indexOf('tile-head-row') < head.indexOf('tile-settings-body'), 'Type, the entity, then Title and Icon, then the body');
assert.doesNotMatch(page, /pictogrammers\.com|admin_icon_label|admin_icon_list|<input type="text" id="\)html";\s*html \+= tab_id;\s*html \+= R"html\(_tile_icon"/,
  'The old icon text field and its link are gone');
assert.match(read('src/tiles/icons/mdi_icons.cpp'), /size_t mdiIconCount\(\) \{ return ICON_COUNT; \}/);
// Position and size only by dragging: hidden inputs keep the values.
for (const field of ['col', 'row', 'span_w', 'span_h']) {
  assert.ok(page.includes(`<input type="hidden" id=")html";\n  html += tab_id;\n  html += R"html(_tile_${field}"`), field);
}
assert.doesNotMatch(page, /tile-layout|layout-field|admin_column|admin_width_cells/, 'No position fields left');
assert.doesNotMatch(read('src/web/assets/admin.css'), /\.tile-layout|\.layout-field/);
// Icon color and Tile color: one row of choices each.
const colors = read('src/web/server/render/tile_icon_colors_html.cpp');
assert.match(colors, /"icon-color-segmented choice-row tile-icon-color-modes"/);
for (const mode of ['"auto", tr.tile_icon_color_auto', '"own", tr.tile_color_mode_custom', '"cover", tr.tile_color_mode_from_cover']) {
  assert.ok(colors.includes(`append_button(html, "", "icon-color-mode", "data-mode", ${mode});`), mode);
}
assert.doesNotMatch(colors, /data-icon-color="clear"/, 'Automatic replaces the reset button');
assert.match(page, /"icon-color-segmented choice-row tile-color-modes"/);
assert.match(read('src/web/server/web_admin.cpp'), /"\/api\/mdi_icons", HTTP_GET,\s*guarded\(\[this\]\(\) \{ this->handleGetMdiIcons\(\); \}\)\)/);
const handler = read('src/web/server/handlers/web_admin_tiles.cpp');
const icons = handler.slice(handler.indexOf('void WebAdminServer::handleGetMdiIcons()'), handler.indexOf('// ========== Folder API'));
assert.match(icons, /setContentLength\(CONTENT_LENGTH_UNKNOWN\)/, 'The list is streamed');
assert.match(icons, /chunk \+= mdiIconName\(i\);\s*chunk \+= '\\n';/);
assert.match(icons, /chunk\.length\(\) >= 1400/, 'Small chunks, no list copy in RAM');
assert.ok(JSON.parse(read('src/web/admin/bundle.json')).sources.includes('src/web/admin/tiles/tile-head.js'));
const scripts = read('src/web/server/render/web_admin_scripts.cpp');
const keys = ['iconPickerAuto', 'iconPickerAutoHint', 'iconPickerNone', 'iconPickerNoneHint', 'iconPickerNoMatch'];
for (const key of [...keys, 'iconPickerSearch']) assert.ok(scripts.includes(`"${key}"`), key);
assert.match(read('src/core/i18n/i18n.h'), /const char\* icon_picker_labels\[5\];/);

// Every language: the last array of each LocaleProfile table.
const i18n = read('src/core/i18n/i18n.cpp');
function lastArray(open) {
  let depth = 0;
  let start = 0;
  let last = '';
  for (let i = open; i < i18n.length; i++) {
    const c = i18n[i];
    if (c === '"') {
      for (i++; i18n[i] !== '"'; i++) if (i18n[i] === '\\') i++;
      continue;
    }
    if (c === '/' && i18n[i + 1] === '/') { i = i18n.indexOf('\n', i); continue; }
    if (c === '{') { depth++; if (depth === 2) start = i; }
    if (c === '}') {
      if (depth === 2) last = i18n.slice(start, i + 1);
      depth--;
      if (depth === 0) break;
    }
  }
  return last;
}
const labels = {};
for (const match of i18n.matchAll(/static const LocaleProfile kLocale(\w+) = \{/g)) {
  const last = lastArray(match.index + match[0].length - 1);
  const strings = [...last.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
  assert.equal(strings.length, 5, `${match[1]} icon picker labels`);
  assert.ok(strings.every(text => text.trim().length) && strings[4].includes('{query}'), `${match[1]} labels`);
  labels[match[1]] = strings;
}
assert.deepEqual(Object.keys(labels).sort(), ['De', 'En', 'Fr', 'Pl']);

// ---- The real bundle in headless Chrome (German) ----
const names = [...read('src/tiles/icons/mdi_icons.cpp').matchAll(/^\s*\{"([a-z0-9-]+)", 0x/gm)].map(m => m[1]);
assert.ok(names.length > 7000, 'The flash table lists every MDI icon');
const de = labels.De;
const appI18n = {
  iconPickerAuto: de[0], iconPickerAutoHint: de[1], iconPickerNone: de[2], iconPickerNoneHint: de[3], iconPickerNoMatch: de[4],
  iconPickerSearch: 'z.B. home', networkError: 'Netzwerkfehler', entityPickerRetry: 'Erneut versuchen',
  entityPickerChoose: 'Entität wählen', entityPickerSearch: 'Suchen', entityPickerClear: 'Leeren'
};
const html = `<!doctype html><html lang="de"><head><style>${read('src/web/assets/admin.css')}</style></head><body>
<div class="tile-settings" id="tSettings" style="width:420px"><div class="tile-settings-head">
<div class="tile-head-top"><h3>Kachel-Einstellungen</h3><select id="t_tile_type" aria-label="Typ"><option value="1">Sensor</option></select></div>
<div class="tile-entity-slot" id="t_tile_entity_slot"></div>
<div class="tile-head-row"><div><label for="t_tile_title">Titel</label><textarea rows="1" class="tile-title-input" spellcheck="false" id="t_tile_title"></textarea></div><div><label for="t_tile_icon_picker">Icon</label><input type="hidden" id="t_tile_icon" data-tile-icon><button type="button" class="tile-icon-picker" id="t_tile_icon_picker" aria-haspopup="listbox"></button></div></div></div>
<div class="tile-settings-body"><input type="color" id="t_tile_icon_color" value="#FFFFFF" data-unset="1">
<div class="icon-color-segmented choice-row" id="t_tile_icon_color_modes"><button>Automatique</button><button>Personnalisée</button><button>De la pochette</button></div>
<div class="icon-color-segmented choice-row" id="t_tile_color_modes"><button>Globale</button><button>Personnalisée</button><button>De l'icône</button><button>De la pochette</button></div>
<div class="type-fields show" id="t_sensor_fields"><label for="t_sensor_entity_picker">Sensor</label><input type="hidden" id="t_sensor_entity" data-entity-picker="sensors"></div></div></div>
<pre id="result"></pre><script>
const loaded=[];const nativeListen=document.addEventListener.bind(document);
document.addEventListener=(name,fn,...args)=>{if(name==='DOMContentLoaded')loaded.push(fn);else nativeListen(name,fn,...args);};
const APP_I18N=${JSON.stringify(appI18n)};const ENTITY_KIND_LABELS=['Sensor'];
const CLIMATE_I18N={},BINARY_SENSOR_I18N={},GRID_COLS=7,GRID_ROWS=5,TILES_PER_GRID=35,ADMIN_WEB_SESSION_TOKEN='test';
const TILE_TYPE_REGISTRY={0:{}};
const TILE_TABS=[],TAB_BY_FOLDER={},FOLDER_BY_TAB={},SCREENSAVER_FOLDER_ID=65535,SCREENSAVER_TILE_DEFAULT_OPACITY=0,SCREENSAVER_TILE_DEFAULT_COLOR='#000000',MEDIA_TILE_TYPE=15,MEDIA_TILE_MIN_SPAN=2,MEDIA_TILE_MAX_SPAN=3;
const NAMES=${JSON.stringify(names)};let iconLoads=0;
window.fetch=async url=>String(url).includes('/api/mdi_icons')
  ?(iconLoads++,{ok:true,text:async()=>NAMES.join('\\n')+'\\n'})
  :{ok:true,json:async()=>({success:true,sensors:[{v:'sensor.battery',t:'Batterie',i:'mdi:battery-80'},{v:'sensor.stars',t:'Sterne',i:'mdi:star'}]})};
${inlineScriptSafe(readAdminDeliverySource())}
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};const $=id=>document.getElementById(id);
 const tick=()=>new Promise(r=>setTimeout(r,0));
 loaded.filter(fn=>/setupEntityPickers\\(document\\)|setupTileHeads\\(document\\)/.test(String(fn))).forEach(fn=>fn());
 placeTileEntityField('t');await fetchEntityOptions();
 const icon=$('t_tile_icon'),button=$('t_tile_icon_picker'),entity=$('t_sensor_entity'),title=$('t_tile_title');
 let inputs=0;nativeListen('input',e=>{if(e.target===icon)inputs++;});
 entity.value='sensor.battery';refreshTileIconButtons('t');
 check(button.querySelector('.mdi-battery-80')&&button.title===${JSON.stringify(de[0])},'Automatic shows the entity icon');
 // Open: Automatic and No icon side by side above the grid, nothing marked in it.
 button.click();await tick();await tick();
 const pop=document.querySelector('.icon-picker-popover.open');
 check(pop&&document.activeElement===pop.querySelector('input'),'Opens with the search focused');
 const choices=[...pop.querySelectorAll('.icon-picker-choice')];
 check(choices.length===2&&choices[0].textContent.includes(${JSON.stringify(de[0])})&&choices[1].textContent.includes(${JSON.stringify(de[2])}),'Both choices in German');
 check(choices[0].classList.contains('selected')&&choices[0].getBoundingClientRect().top===choices[1].getBoundingClientRect().top,'Automatic chosen, both on one row');
 check(pop.querySelectorAll('.icon-picker-cell').length===240&&!pop.querySelector('.icon-picker-cell.active'),'The first icons, none marked');
 check(iconLoads===1,'The list is loaded once');
 const list=pop.querySelector('.icon-picker-list');list.scrollTop=list.scrollHeight;list.dispatchEvent(new Event('scroll'));await tick();
 check(pop.querySelectorAll('.icon-picker-cell').length===480,'More icons while scrolling');
 // Search, arrows and Enter choose with one input event.
 const search=pop.querySelector('input');search.value='battery charging';search.dispatchEvent(new Event('input',{bubbles:true}));
 const expected=NAMES.filter(n=>n.includes('battery')&&n.includes('charging'));
 check(pop.querySelectorAll('.icon-picker-cell').length===expected.length&&!pop.querySelector('.icon-picker-choice'),'Search filters the panel list');
 search.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));
 search.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));
 check(icon.value===expected[1]&&inputs===1&&!document.querySelector('.icon-picker-popover.open'),'Arrow and Enter choose: '+icon.value);
 check(button.querySelector('.mdi-'+expected[1]),'The button shows the chosen icon');
 // No match, then No icon, then Automatic again.
 button.click();await tick();
 const s2=document.querySelector('.icon-picker-popover input');s2.value='zzzz';s2.dispatchEvent(new Event('input',{bubbles:true}));
 check(document.querySelector('.icon-picker-popover').textContent.includes(${JSON.stringify(de[4].replace('{query}', 'zzzz'))}),'No match names the search');
 s2.value='';s2.dispatchEvent(new Event('input',{bubbles:true}));
 check(document.querySelector('.icon-picker-cell.selected')?.dataset.icon===expected[1],'The chosen icon is marked');
 document.querySelector('.icon-picker-choice[data-icon="none"]').click();
 check(icon.value==='none'&&button.classList.contains('none')&&button.querySelector('.mdi-cancel')&&inputs===2,'No icon');
 button.click();await tick();document.querySelector('.icon-picker-choice[data-icon=""]').click();
 check(icon.value===''&&button.querySelector('.mdi-battery-80')&&!button.classList.contains('none'),'Automatic again');
 // Loads and drafts set the value; the button follows. Legacy values too.
 icon.value='mdi:home';check(button.querySelector('.mdi-home'),'A loaded icon shows');
 icon.value='-';check(button.classList.contains('none'),'A legacy disabled icon shows as No icon');
 icon.value='';
 // A chosen entity brings its icon to the button.
 $('t_sensor_entity_picker').click();await tick();
 [...document.querySelectorAll('.entity-picker-item')].find(i=>i.textContent.includes('Sterne')).click();
 check(button.querySelector('.mdi-star')&&title.value==='Sterne','The entity brings name and icon');
 // Escape and outside clicks close; the two pickers never stay open together.
 button.click();await tick();
 document.querySelector('.icon-picker-popover input').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}));
 check(!document.querySelector('.icon-picker-popover.open')&&document.activeElement===button,'Escape closes');
 button.click();await tick();$('t_sensor_entity_picker').click();await tick();
 check(!document.querySelector('.icon-picker-popover.open')&&document.querySelector('.entity-picker-popover.open'),'Opening the entity list closes the icons');
 button.click();await tick();
 check(document.querySelector('.icon-picker-popover.open')&&!document.querySelector('.entity-picker-popover.open'),'Opening the icons closes the entity list');
 document.body.click();check(!document.querySelector('.icon-picker-popover.open'),'Outside click closes');
 // Title: one line, a second by Enter, never a third.
 check(title.classList.contains('two')===false,'One line');
 const first=new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true});title.dispatchEvent(first);
 check(!first.defaultPrevented,'Enter makes the second line');
 title.value='Haus\\nBatterie';check(title.classList.contains('two'),'Two lines grow the field');
 const third=new KeyboardEvent('keydown',{key:'Enter',shiftKey:true,bubbles:true,cancelable:true});title.dispatchEvent(third);
 check(third.defaultPrevented,'No third line');
 const tall=title.getBoundingClientRect().height;title.value='Haus';
 check(!title.classList.contains('two')&&title.getBoundingClientRect().height<tall,'One line again');
 check(Math.round(button.getBoundingClientRect().top)===Math.round(title.getBoundingClientRect().top),'Icon beside the first line');
 // Rows of choices: every label on one line, French with four choices too.
 fitChoiceRows('t');
 for(const id of ['t_tile_icon_color_modes','t_tile_color_modes']){
  const buttons=[...$(id).querySelectorAll('button')];
  check(buttons.every(b=>b.scrollWidth<=b.clientWidth+0.5),id+' overflows');
  const size=parseFloat(getComputedStyle(buttons[0]).fontSize);
  check(size>=10&&size<=12,id+' font '+size);
  check(new Set(buttons.map(b=>Math.round(b.getBoundingClientRect().top))).size===1,id+' one row');
 }
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';document.getElementById('result').textContent=error.stack;}})();
</script></body></html>`;
let domChecked = false;
try {
  domChecked = runDomHarness({label: 'Tile head', html, tmpPrefix: 'hometiles-tile-head-',
    extraArgs: ['--virtual-time-budget=5000']}) !== false;
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 600));
}
console.log(`Tile head: server contract, translations${domChecked ? ', editor DOM' : ''} pass`);
