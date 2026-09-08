import fs from 'node:fs';
import path from 'node:path';
import {extractDeliveredFunction, inlineScriptSafe, readRepoFile, repoRoot} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const helpers = [
  'escapeHtml', 'normalizeTileTitle', 'tileTitleHtml', 'getTileTypeMeta',
  'isEditablePreview', 'normalizeSensorValueFont', 'getSensorValueFontClass',
  'resolveIconName', 'normalizeMdiIconName', 'isExplicitlyDisabledValue',
  'tileColorInputIsDefault', 'tileBgToHex', 'rgbToHex', 'applyTileAriaLabel',
  'resolveUnitValue', 'isScreensaverTileTab', 'getTileResizeHandlesHtml',
  'renderTileFromData', 'updateTilePreview'
].map(extractDeliveredFunction).join('\n');
const html = `<!doctype html><html><head><style>
${readRepoFile('src/web/assets/admin.css')}
body { padding:20px; } #fixtures { display:flex; flex-wrap:wrap; gap:16px; }
</style></head><body><div id="inputs"></div><div id="fixtures"></div><pre id="result"></pre><script>
${inlineScriptSafe(helpers)}
const TILE_TYPE_REGISTRY = {
  8:{css:'sensor',preview:'sensor'}, 12:{css:'weather',preview:'weather'},
  15:{css:'media',preview:'media'}
};
let currentTileIndex=0, currentTileTab='fixture';
const HIDDEN_SETTINGS_TILE_INDEX=-2;
const sensorMetaCache={values:{},units:{},icons:{},names:{}};
// Grid placement is fixed by the fixture; execute both real markup branches.
function updateLayoutFromInputs() {}
try {
 const check=(value,message)=>{if(!value)throw Error(message);};
 for(const name of ['title','color','type','icon']) {
  const input=document.createElement(name==='title'?'textarea':'input');
  input.id='fixture_tile_'+name;document.querySelector('#inputs').append(input);
 }
 document.querySelector('#fixture_tile_color').value='#223344';
 document.querySelector('#fixture_tile_icon').value='weather-sunny';
 const fixture=document.createElement('div');fixture.id='fixture-tile-0';
 document.querySelector('#fixtures').append(fixture);
 let cases=0;
 for(const scale of [.5,.75,1])for(const [width,height] of [[168,145],[352,306],[720,306]]) {
  fixture.style.cssText='width:'+width*scale+'px;height:'+height*scale+'px;'+
   '--fs20:'+20*scale+'px;--icon-size:'+48*scale+'px;--tile-pad-h:'+20*scale+'px;'+
   '--tile-header-title-top:'+28*scale+'px;--tile-header-title-right:'+16*scale+'px;'+
   '--tile-header-icon-top:'+16*scale+'px;--tile-header-icon-left:'+12*scale+'px;';
  for(const mode of ['cached','live'])for(const title of ['Weather','Weather\\nHome']) {
   let reference;
   for(const type of [8,12,15]) {
    document.querySelector('#fixture_tile_title').value=title;
    document.querySelector('#fixture_tile_type').value=String(type);
    if(mode==='live') updateTilePreview('fixture');
    else renderTileFromData('fixture',0,{type,title,icon_name:'weather-sunny'},sensorMetaCache);
    const tile=fixture.getBoundingClientRect();
    const label=fixture.querySelector('.tile-title-lines').getBoundingClientRect();
    const icon=fixture.querySelector('.tile-icon').getBoundingClientRect();
    const geometry=[label.top-tile.top,label.right-tile.left,icon.top-tile.top,icon.left-tile.left];
    if(reference) check(geometry.every((value,index)=>Math.abs(value-reference[index])<.1),mode+' headers must match Sensor');
    else reference=geometry;
    const center=label.top-tile.top+label.height/2;
    check(Math.abs(center-(28+13.5)*scale)<.1,mode+' title must use the device anchor for one/two lines');
    check(Math.abs(icon.top-tile.top-16*scale)<.1,mode+' icon must use the device inset');
    check(label.top>tile.top+6*scale,mode+' two-line title must clear the top edge');
    ++cases;
   }
  }
  // Every type using the Sensor-style header must consume the same CSS rule.
  for(const type of ['sensor','binary_sensor','number','select','datetime',
                     'cover','energy','weather','media','climate']) {
   fixture.className='tile '+type;
   fixture.innerHTML='<i class="mdi tile-icon"></i><div class="tile-title">'+tileTitleHtml('Room\\nHome')+'</div>';
   const tile=fixture.getBoundingClientRect();
   const label=fixture.querySelector('.tile-title-lines').getBoundingClientRect();
   const icon=fixture.querySelector('.tile-icon').getBoundingClientRect();
   check(Math.abs(label.top-tile.top+label.height/2-41.5*scale)<.1,type+' title anchor');
   check(Math.abs(icon.top-tile.top-16*scale)<.1,type+' icon inset');
  }
 }
 document.body.dataset.result='pass';document.querySelector('#result').textContent=cases+' live/cached header cases passed';
} catch(error){document.body.dataset.result='fail';document.querySelector('#result').textContent=error.stack;}
</script></body></html>`;
const out=path.join(repoRoot,'build/tests/preview-header-alignment');
fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'index.html'),html);
runDomHarness({label:'Preview headers match Sensor and device coordinates',html,tmpPrefix:'hometiles-preview-header-'});
