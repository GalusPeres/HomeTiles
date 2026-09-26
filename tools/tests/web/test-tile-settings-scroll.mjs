// Tile Settings keep the clicked control in place. Scrolled to the end of
// the settings body, a choice that hides fields below it (Tile color Global
// hides the From icon strength, Rules Off hides their body, ...) shortened
// the body; the browser clamped the scroll position and the clicked choice
// slid down. Runs the delivered module in headless Chrome: segmented buttons
// and checkbox labels stay put, a spacer makes room and shrinks away again
// while scrolling up, and a longer body needs no spacer. A long entity name
// (no break in the entity ID) never widens the panel: the field stays one
// line with an ellipsis instead of pushing the body into horizontal scroll.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const module = read('src/web/admin/tiles/settings-scroll.js');
assert.ok(read('src/web/admin/bundle.json').includes('"src/web/admin/tiles/settings-scroll.js",'), 'Bundled');
assert.ok(readAdminDeliverySource().includes('const SETTINGS_SCROLL_SPACER="tile-settings-scroll-spacer";'), 'Delivered');

const page = `<!doctype html><html lang="en"><head><style>${readRepoFile('src/web/assets/admin.css')}
.tile-settings{height:320px;max-height:320px;position:static}
</style></head><body>
<div class="tile-settings"><div class="tile-specific-settings"><div class="tile-settings-body" id="body">
<div style="height:420px">fields above</div>
<div class="icon-color-segmented"><button type="button" id="global">Global</button><button type="button" id="icon">From icon</button></div>
<div id="strength" style="height:160px">strength</div>
<label class="inline-checkbox" id="rules_label"><input type="checkbox" id="rules" checked> Rules</label>
<div id="rules_body" style="height:220px">rules</div>
<div style="height:40px">end</div>
</div></div></div>
<div class="tile-settings" style="height:200px;max-height:200px;position:static"><div class="tile-specific-settings"><div class="tile-settings-body" id="entity_body">
<div class="type-fields show"><label>Binary sensor entity</label>
<select id="x_binary_sensor_entity"><option>Schreibtisch - binary_sensor.presence_sensor_fp2_9514_presence_sensor_2</option></select>
<label>Size</label><select id="x_size"><option>Default</option></select></div></div></div></div>
<pre id="result"></pre><script>
// Headless --dump-dom renders a single animation frame; later frames never
// come. A timer stands in for them so every step runs the module's callback.
window.requestAnimationFrame=callback=>setTimeout(()=>callback(performance.now()),0);
${inlineScriptSafe(module)}
const $=id=>document.getElementById(id);
$('global').addEventListener('click',()=>$('strength').classList.add('hidden'));
$('icon').addEventListener('click',()=>$('strength').classList.remove('hidden'));
$('rules').addEventListener('change',()=>$('rules_body').classList.toggle('hidden',!$('rules').checked));
const frame=()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
// Headless dispatches no scroll events without frames either.
const scrolled=()=>document.getElementById('body').dispatchEvent(new Event('scroll'));
const spacer=()=>parseFloat(document.querySelector('.tile-settings-scroll-spacer')?.style.height||'0')||0;
(async()=>{try{
 const check=(v,m)=>{if(!v)throw Error(m);};
 const body=$('body');
 body.scrollTop=body.scrollHeight;await frame();
 const top=$('global').getBoundingClientRect().top;
 $('global').click();await frame();
 check(Math.abs($('global').getBoundingClientRect().top-top)<1,'Global stays in place: '+top+' -> '+$('global').getBoundingClientRect().top);
 check(spacer()>0,'A spacer makes room');
 // A checkbox label near the end: the label click and its change are one gesture.
 body.scrollTop=body.scrollHeight;await frame();
 const labelTop=$('rules_label').getBoundingClientRect().top;
 $('rules_label').click();await frame();
 check($('rules_body').classList.contains('hidden'),'The label toggled the rules');
 check(Math.abs($('rules_label').getBoundingClientRect().top-labelTop)<1,'The label stays in place');
 // Scrolling up trims the spacer without moving the view.
 const before=spacer();
 body.scrollTop=Math.max(0,body.scrollTop-150);scrolled();await frame();
 check(spacer()<before,'Scrolling up shrinks the spacer: '+before+' -> '+spacer());
 body.scrollTop=0;scrolled();await frame();
 check(spacer()===0,'At the top the spacer is gone: '+spacer());
 // Opening fields below needs no spacer and keeps the control too.
 body.scrollTop=body.scrollHeight;await frame();
 const iconTop=$('icon').getBoundingClientRect().top;
 $('icon').click();await frame();
 check(Math.abs($('icon').getBoundingClientRect().top-iconTop)<1,'From icon stays in place');
 // Long entity names end in an ellipsis on one line; no horizontal scroll.
 const entityBody=$('entity_body'),entity=$('x_binary_sensor_entity');
 check(entityBody.scrollWidth<=entityBody.clientWidth,'No horizontal overflow: '+entityBody.scrollWidth+' > '+entityBody.clientWidth);
 check(entity.getBoundingClientRect().right<=entityBody.getBoundingClientRect().right+0.5,'The field stays inside the panel');
 check(Math.abs(entity.getBoundingClientRect().height-$('x_size').getBoundingClientRect().height)<1,'One line like the other fields');
 check(getComputedStyle(entity).textOverflow==='ellipsis','Ellipsis');
 document.body.dataset.result='pass';
}catch(error){document.body.dataset.result='fail';$('result').textContent=error.stack;}})();
</script></body></html>`;
try {
  runDomHarness({label: 'Tile Settings scroll', html: page, tmpPrefix: 'hometiles-settings-scroll-',
    extraArgs: ['--virtual-time-budget=3000', '--window-size=1400,900']});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 400));
}
console.log('Tile Settings keep the clicked control in place; long entity names stay inside the panel');
