import assert from 'node:assert/strict';
import {extractDeliveredFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const editor = readRepoFile('src/web/admin/tiles/editor.js');
assert(editor.includes('normalizeTileTitle(titleInput.value)'));
assert.match(readRepoFile('src/web/server/render/web_admin_html.cpp'), /<textarea rows="2" class="tile-title-input"/);
const helpers = ['escapeHtml', 'normalizeTileTitle', 'tileTitleHtml'].map(extractDeliveredFunction).join('\n');
const html = `<!doctype html><html><head><style>${readRepoFile('src/web/assets/admin.css')}
body {padding:12px;} #fixtures {display:flex;flex-wrap:wrap;gap:12px;} .tile {position:relative;box-sizing:border-box;}
</style></head><body><div id="fixtures"></div><pre id="result"></pre><script>
${inlineScriptSafe(helpers)}
try {
 const check=(v,m)=>{if(!v)throw Error(m);};
 const types=['sensor','binary_sensor','number','select','datetime','cover','climate','energy','weather','media','scene','navigate','switch','camera','animation','clock','text'];
 for(const type of types)for(const width of [110,240]){
  const tile=document.createElement('div');tile.className='tile '+type;tile.style.cssText='width:'+width+'px;height:112px;--fs20:14px;';
  tile.innerHTML='<div class="tile-title">'+tileTitleHtml('Room')+'</div>';document.querySelector('#fixtures').append(tile);
  const title=tile.querySelector('.tile-title');const line=title.firstChild.getBoundingClientRect();
  title.innerHTML=tileTitleHtml('Room\\nOffice');
  const two=title.firstChild.getBoundingClientRect();
  check(Math.abs(line.top+line.height/2-two.top-two.height/2)<1,type+' title center moved');
  check(title.querySelectorAll('.tile-title-line').length===2,type+' needs two explicit lines');
  title.innerHTML=tileTitleHtml('A long title which must clip without creating automatic additional lines');
  const long=title.querySelector('.tile-title-line');check(getComputedStyle(long).textOverflow==='ellipsis',type+' needs ellipsis');
  check(long.scrollWidth>long.clientWidth,type+' test must exercise actual overflow');
  title.innerHTML=tileTitleHtml('<img src=x>\\nOffice');check(!title.querySelector('img'),'Title markup escaped');
 }
 check(normalizeTileTitle('First\\r\\nSecond\\nThird')==='First\\nSecond Third','At most two lines');
 check(normalizeTileTitle('First\\\\nSecond')==='First\\nSecond','Legacy line break');
 const bounded=normalizeTileTitle('Ä'.repeat(200));check(new TextEncoder().encode(bounded).length===254,'UTF-8 byte boundary');
 document.body.dataset.result='pass';document.querySelector('#result').textContent='34 tile geometry cases passed';
} catch(e){document.body.dataset.result='fail';document.querySelector('#result').textContent=e.stack;}
</script></body></html>`;
runDomHarness({label:'Two-line titles, centers, overflow and escaping',html,tmpPrefix:'hometiles-titles-'});
