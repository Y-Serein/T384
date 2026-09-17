// Execute the real inline UI with a deterministic DOM/canvas fixture, no device.
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../web/raw16_bench_console.html'), 'utf8');
let script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const ctx = {clearRect(){}, fillRect(){}, strokeRect(){}, beginPath(){}, moveTo(){}, lineTo(){}, stroke(){}, fillText(){},
  measureText(text){return {width:text.length * 6};}, putImageData(){},
  createImageData(w,h){return {data: new Uint8ClampedArray(w*h*4)};}};
function element(id) {
  if (!elements.has(id)) elements.set(id, {id, width:384, height:288, checked:true, textContent:'', innerHTML:'', disabled:false,
    listeners:{}, style:{}, parentElement:{style:{}}, getContext(){return ctx;},
    getBoundingClientRect(){return {left:0,top:0,width:384,height:288};},
    setPointerCapture(){}, setAttribute(){}, addEventListener(type,fn){this.listeners[type]=fn;}});
  return elements.get(id);
}
for (const match of html.matchAll(/id="([^"]+)"/g)) element(match[1]);
for (const match of html.matchAll(/<[^>]*id="([^"]+)"[^>]*\bdisabled\b[^>]*>/g)) element(match[1]).disabled=true;
const tail = script.lastIndexOf('    updateRecordControls();\n    openDatabase()');
assert(tail > 0);
script = script.slice(0,tail) + `
    globalThis.ui = {
      setup(w,h,format,zero=NaN,counts=NaN) {
        RAW16_WIDTH=w; RAW16_HEIGHT=h; streamPixelFormat=format;
        y16ZeroC=zero; y16CountsPerC=counts; rawImageData=null;
        lastDisplayFrame=null; focusRoi=null; streamLoaded=true;
      }, renderRaw16Frame, regionStats, pointerPixel, updateRoi, detectCapabilities,
      roi() { return focusRoi; }, frame() { return lastDisplayFrame; }
    };
  })();`;
const sandbox = {document:{getElementById:element, querySelectorAll(){return [];}, addEventListener(){}},
  location:{host:'192.168.17.1',origin:'http://192.168.17.1'},
  addEventListener(){}, setTimeout,clearTimeout,setInterval,clearInterval, AbortController,
  fetch:async()=>({ok:true,json:async()=>({network:{device_ip:'192.168.17.1'},ota:{supported:false}})}),
  console, Uint8Array, Uint32Array, Uint8ClampedArray, URL};
vm.runInNewContext(script,sandbox);
const ui = sandbox.ui;
for (const [w,h] of [[256,192],[384,288]]) {
  ui.setup(w,h,2);
  const frame = new Uint8Array(w*h*2);
  for (let i=0;i<w*h;i++) {frame[i*2]=0x27; frame[i*2+1]=0x10;}
  frame[0]=0;frame[1]=1; frame[frame.length-2]=0xff;frame[frame.length-1]=0xff;
  const original = frame.slice(); ui.renderRaw16Frame(frame);
  assert.deepEqual(frame,original); // Display never mutates raw data.
  assert.match(element('minimumPosition').textContent,/\(0,0\).*Y16 1$/);
  assert.match(element('maximumPosition').textContent,new RegExp('\\('+ (w-1)+','+(h-1)+'\\).*65535$'));
  assert.equal(element('maximumTemperature').innerHTML,'-- <small>°C</small>');
  assert.equal(element('centerY16').textContent,'10000.0');
  frame.fill(0); assert.deepEqual(ui.frame(),original); // Partial next frame cannot change ROI/snapshot.
  assert.equal(ui.regionStats(original,{x0:10,y0:10,x1:20,y1:20}),10000);
}
ui.setup(384,288,2); const frame=new Uint8Array(384*288*2).fill(10);ui.renderRaw16Frame(frame);
const overlay=element('annotations');
overlay.listeners.pointerdown({button:0,pointerId:1,clientX:300,clientY:250,preventDefault(){}});
overlay.listeners.pointermove({pointerId:1,clientX:-10,clientY:-20});
assert.equal(JSON.stringify(ui.roi()),JSON.stringify({x0:0,y0:0,x1:300,y1:250}));
overlay.listeners.pointerup(); assert.match(element('roiSummary').textContent,/301×251/);
overlay.listeners.keydown({key:'Escape'}); assert.equal(ui.roi(),null);
element('centerRoiBtn').listeners.click(); assert.match(element('roiSummary').textContent,/32×32/);
element('clearRoiBtn').listeners.click(); assert.equal(ui.roi(),null);
ui.setup(256,192,2,0,100);ui.renderRaw16Frame(new Uint8Array(256*192*2).fill(10));
assert.match(element('centerTemperature').innerHTML,/25\.7/);
ui.setup(384,288,3);ui.renderRaw16Frame(frame);
assert.equal(element('minimumPosition').textContent,'--'); assert.equal(element('roiY16').textContent,'--');
ui.detectCapabilities().then(()=> {
  assert.equal(element('ipFallback').href,'http://192.168.17.1/');
  assert.match(element('otaInfo').textContent,/未提供/); assert.equal(element('otaUploadBtn').disabled,true);
  console.log('device UI 256/384 raw integrity/ROI/pointer/extrema/no-model/Picture/capability smoke passed');
}).catch(error=>{console.error(error);process.exitCode=1;});
