import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const [pagePath, work, port = '9237'] = process.argv.slice(2);
const assert = (value, message) => { if (!value) throw new Error(message); };
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
let version;
for (let i = 0; i < 100; ++i) {
  try { version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); break; }
  catch (_) { await pause(100); }
}
assert(version, 'Headless browser did not start');
const socket = new WebSocket(version.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, {once:true}); socket.addEventListener('error', reject, {once:true}); });
let sequence = 0;
const pending = new Map();
const exceptions = [];
socket.addEventListener('message', event => {
  const message = JSON.parse(event.data);
  if (message.method === 'Runtime.exceptionThrown') exceptions.push(message.params.exceptionDetails);
  if (!message.id) return;
  const callbacks = pending.get(message.id);
  if (callbacks) { pending.delete(message.id); message.error ? callbacks.reject(new Error(JSON.stringify(message.error))) : callbacks.resolve(message.result); }
});
function send(method, params = {}, sessionId) {
  const id = ++sequence;
  return new Promise((resolve, reject) => {
    pending.set(id, {resolve, reject}); socket.send(JSON.stringify({id, method, params, ...(sessionId ? {sessionId} : {})}));
  });
}
const downloads = path.resolve(work, 'downloads');
await fs.mkdir(downloads, {recursive:true});
try {
  await send('Browser.setDownloadBehavior', {behavior:'allow', downloadPath:downloads});
  const {targetId} = await send('Target.createTarget', {url:'about:blank'});
  const {sessionId} = await send('Target.attachToTarget', {targetId, flatten:true});
  const call = (method, params) => send(method, params, sessionId);
  const evaluate = async expression => {
    const result = await call('Runtime.evaluate', {expression, returnByValue:true, awaitPromise:true, userGesture:true});
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  await call('Page.enable'); await call('Runtime.enable');
  await call('Emulation.setDeviceMetricsOverride', {width:1440, height:1080, deviceScaleFactor:1, mobile:false});
  await call('Page.navigate', {url:pathToFileURL(path.resolve(pagePath)).href});
  async function ready() {
    for (let i=0;i<100;++i) {
      if (await evaluate(`document.readyState==='complete' && typeof selectionDocument==='function' && document.querySelectorAll('.card').length===484`)) return;
      await pause(100);
    }
    throw new Error('Texture picker did not finish loading');
  }
  await ready();
  assert(await evaluate(`document.querySelectorAll('.card select').length===484 && document.querySelectorAll('.compare input[type=range]').length===484`), 'Every texture needs a selector and a comparison slider');
  assert(await evaluate(`catalog.textures.filter(t=>t.defaultVersion==='ps1').length===262 && catalog.textures.filter(t=>t.defaultVersion==='pc').length===222`), 'Current choices differ from the audited selection');
  for(let i=0;i<100;++i){if(await evaluate(`Array.from(document.querySelectorAll('.card:first-child img')).every(i=>i.complete && i.naturalWidth>0)`))break;await pause(100);}
  assert(await evaluate(`Array.from(document.querySelectorAll('.card:first-child img')).every(i=>i.complete && i.naturalWidth>0)`), 'Comparison preview did not load');
  const slider = await evaluate(`(()=>{const r=document.querySelector('.card:nth-child(3) input[type=range]').getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}})()`);
  await call('Input.dispatchMouseEvent', {type:'mousePressed', x:slider.x+slider.w*.5, y:slider.y+slider.h*.5, button:'left', clickCount:1});
  await call('Input.dispatchMouseEvent', {type:'mouseMoved', x:slider.x+slider.w*.73, y:slider.y+slider.h*.5, button:'left', buttons:1});
  await call('Input.dispatchMouseEvent', {type:'mouseReleased', x:slider.x+slider.w*.73, y:slider.y+slider.h*.5, button:'left', clickCount:1});
  assert(await evaluate(`(()=>{const s=document.querySelector('.card:nth-child(3) input[type=range]');return +s.value>=68 && +s.value<=78 && s.parentElement.style.getPropertyValue('--split')===s.value+'%'})()`), 'Dragging the comparison did not move the PS1 overlay');
  await evaluate(`document.querySelector('.card select').value='pc';document.querySelector('.card select').dispatchEvent(new Event('change'));`);
  assert(await evaluate(`selectionDocument().choices[0].version==='pc' && document.querySelector('.card').classList.contains('edited')`), 'Selector did not record the PC choice');
  await evaluate(`document.querySelector('#changed').checked=true;document.querySelector('#changed').dispatchEvent(new Event('change'));`);
  assert(await evaluate(`Array.from(document.querySelectorAll('.card')).filter(c=>!c.hidden).length===1`), 'Changed-only filter lost a choice');
  await evaluate(`document.querySelector('#changed').checked=false;document.querySelector('#changed').dispatchEvent(new Event('change'));document.querySelector('#group').value='Scenery';document.querySelector('#group').dispatchEvent(new Event('change'));`);
  assert(await evaluate(`Array.from(document.querySelectorAll('.card')).filter(c=>!c.hidden).every(c=>c.dataset.textureId.startsWith('TRACK'))`), 'Category filter shows unrelated textures');
  await evaluate(`document.querySelector('#group').value='';document.querySelector('#group').dispatchEvent(new Event('change'));document.querySelector('#search').value='valparaiso';document.querySelector('#search').dispatchEvent(new Event('input'));`);
  assert(await evaluate(`Array.from(document.querySelectorAll('.card')).filter(c=>!c.hidden).length>10`), 'Track search failed');
  await evaluate(`document.querySelector('#search').value='';document.querySelector('#search').dispatchEvent(new Event('input'));`);
  const exportBox = await evaluate(`(()=>{const r=document.querySelector('#export').getBoundingClientRect();return {x:r.x,y:r.y,right:r.right}})()`);
  assert(exportBox.x>900 && exportBox.y<50 && exportBox.right<=1440, 'Export button is not at the top right');
  await fs.writeFile(path.join(work, 'desktop.png'), Buffer.from((await call('Page.captureScreenshot', {format:'png'})).data, 'base64'));
  await evaluate(`document.querySelector('#export').click()`);
  let exported;
  for (let i=0;i<100;++i) {
    exported=(await fs.readdir(downloads)).find(name=>name.endsWith('.json'));
    if(exported) break; await pause(100);
  }
  assert(exported, 'Export button did not download a JSON file');
  const data=JSON.parse(await fs.readFile(path.join(downloads,exported),'utf8'));
  assert(data.format==='wo4k.texture-selection' && data.version===1 && data.choices.length===484 && data.choices[0].version==='pc', 'Export lost selected or filtered-out textures');
  assert(new Set(data.choices.map(c=>c.id)).size===484 && data.choices.every(c=>c.version==='ps1'||c.version==='pc'), 'Export contains invalid or duplicate choices');
  await call('Page.reload'); await ready();
  assert(await evaluate(`selectionDocument().choices[0].version==='pc'`), 'Choice was not retained after reload');
  await call('Emulation.setDeviceMetricsOverride', {width:390, height:844, deviceScaleFactor:1, mobile:false});
  assert(await evaluate(`document.documentElement.scrollWidth<=innerWidth && document.querySelector('#export').getBoundingClientRect().right<=innerWidth`), 'Mobile layout overflows');
  await fs.writeFile(path.join(work,'mobile.png'), Buffer.from((await call('Page.captureScreenshot',{format:'png'})).data,'base64'));
  await evaluate(`document.querySelector('#reset').click()`);
  assert(await evaluate(`textures.every(t=>choices.get(t.id)===t.defaultVersion)`), 'Reset did not restore the current defaults');
  assert(exceptions.length===0, 'Page raised JavaScript errors');
  console.log(JSON.stringify({result:'PASS',textures:484,checks:['real slider drag','per-texture selectors','category/search/changed filters','complete JSON download','reload persistence','reset','desktop/mobile layout'],exported:path.join(downloads,exported),screenshots:[path.join(work,'desktop.png'),path.join(work,'mobile.png')]}));
} finally {
  await send('Browser.close').catch(()=>{}); socket.close();
}
