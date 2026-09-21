// Exercise the real console's asynchronous stream lifecycle without a device.
const fs = require('node:fs'), vm = require('node:vm'), assert = require('node:assert/strict');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../web/raw16_bench_console.html'), 'utf8');
const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const tail = source.lastIndexOf('    updateRecordControls();\n    openDatabase()');
assert(tail > 0);
const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };
function fixture() {
  const elements = new Map(), events = {}, documentEvents = {}, timers = new Map(), requests = [];
  let timerId = 0, now = 100;
  const context = {clearRect(){}, fillRect(){}, strokeRect(){}, beginPath(){}, moveTo(){}, lineTo(){}, stroke(){}, fillText(){},
    measureText(text){return {width:text.length * 6};}, putImageData(){},
    createImageData(w,h){return {data:new Uint8ClampedArray(w*h*4)};}};
  function element(id) {
    if (!elements.has(id)) elements.set(id, {id, width:384, height:288, checked:true, listeners:{}, style:{}, parentElement:{style:{}},
      getContext(){return context;}, setAttribute(){}, addEventListener(type,fn){this.listeners[type]=fn;}});
    return elements.get(id);
  }
  const document = {hidden:false, getElementById:element, querySelectorAll(){return [];},
    addEventListener(type,fn){documentEvents[type]=fn;}};
  const sandbox = {document, location:{host:'192.168.17.1',origin:'http://192.168.17.1'},
    performance:{now:()=>now}, addEventListener(type,fn){events[type]=fn;}, AbortController,
    setTimeout(fn,delay){const id=++timerId;timers.set(id,{fn,delay});return id;},
    clearTimeout(id){timers.delete(id);}, setInterval(){return ++timerId;}, clearInterval(){},
    fetch(url,options) {
      if (!url.startsWith('/raw16.stream')) return Promise.reject(new Error('offline'));
      return new Promise((resolve,reject)=>requests.push({resolve,reject,signal:options.signal}));
    }, console, Uint8Array, Uint32Array, Uint8ClampedArray, URL};
  vm.runInNewContext(source.slice(0,tail) + `
    globalThis.ui = {startStream, streamWatchdog,
      accept() { acceptRaw16Frame(new Uint8Array(RAW16_WIDTH*RAW16_HEIGHT*2), 1); },
      state() { return {loaded:streamLoaded, active:!!streamController,
        frames:browserFrameCount, discarded:browserDiscardedFrames, sequence:previousFrameIndex,
        prefix:lastDisplayFrame ? Array.from(lastDisplayFrame.subarray(0,4)) : null}; }};
  })();`, sandbox);
  async function run(delay) {
    const entry=[...timers].find(([,timer])=>timer.delay===delay);
    assert(entry, 'expected retry timer with delay '+delay);
    timers.delete(entry[0]); entry[1].fn(); await flush();
  }
  function response(request, width=384) {
    const headers = {'X-T384-Format':'T384-FRAME-CHUNK-V1','X-T384-Wire-Version':'1',
      'X-T384-Chunk-Header-Bytes':'36','X-T384-Frame-Width':String(width),
      'X-T384-Frame-Height':width===384?'288':'192','X-T384-Frame-Bytes':String(width*(width===384?288:192)*2),
      'X-T384-Chunk-Payload-Max':'6144','X-T384-Pixel-Format-Code':'2','X-T384-Frame-Mode':'tpd'};
    const reader = {pending:null, cancelled:0, released:0,
      read(){return new Promise((resolve,reject)=>{this.pending={resolve,reject};});},
      cancel(){this.cancelled++;if(this.pending)this.pending.resolve({done:true});return Promise.resolve();},
      releaseLock(){this.released++;}};
    request.resolve({ok:true,headers:{get:key=>headers[key]||null},body:{getReader:()=>reader}});
    return reader;
  }
  return {ui:sandbox.ui, document, events, documentEvents, requests, timers, element, run, response,
    advance(ms){now+=ms;}};
}
(async()=>{
  // A block ring may send an aborted prefix. Only a complete physical END
  // may publish a picture, and the next START must discard the old prefix.
  const ring=fixture(); ring.ui.startStream(); await ring.run(0);
  const ringReader=ring.response(ring.requests[0]); await flush();
  function wireChunk(sequence, block, end=false) {
    const bytes=new Uint8Array(36+6144), header=new DataView(bytes.buffer);
    header.setUint32(0,0x31523354,true); header.setUint16(4,1,true); header.setUint16(6,36,true);
    header.setUint32(8,sequence,true); header.setUint32(12,block*6144,true);
    header.setUint32(16,384*288*2,true); header.setUint32(20,100,true);
    header.setUint16(24,6144,true); header.setUint16(26,384,true); header.setUint16(28,288,true);
    header.setUint16(30,0x10|(block===0?1:0)|(end?2:0),true); header.setUint16(32,2,true);
    let crc=0;
    for(let i=0;i<34;i++) {
      crc ^= bytes[i]<<8;
      for(let bit=0;bit<8;bit++) crc=((crc<<1)^((crc&0x8000)?0x1021:0))&0xFFFF;
    }
    header.setUint16(34,crc,true);
    for(let i=36;i<bytes.length;i+=2) { bytes[i]=0x7A; bytes[i+1]=sequence; }
    return bytes;
  }
  async function deliver(bytes) {
    ringReader.pending.resolve({done:false,value:bytes.subarray(0,7)}); await flush();
    ringReader.pending.resolve({done:false,value:bytes.subarray(7)}); await flush();
  }
  await deliver(wireChunk(41,0)); await deliver(wireChunk(41,1));
  assert.equal(ring.ui.state().frames,0); assert.equal(ring.ui.state().loaded,false);
  for(let block=0;block<35;block++) await deliver(wireChunk(42,block));
  assert.equal(ring.ui.state().discarded,1); assert.equal(ring.ui.state().frames,0);
  await deliver(wireChunk(42,35,true));
  assert.equal(ring.ui.state().frames,1); assert.equal(ring.ui.state().sequence,42);
  assert.equal(ring.ui.state().loaded,true);
  assert.deepEqual(Array.from(ring.ui.state().prefix),[0x7A,42,0x7A,42]);
  ring.events.pagehide(); await flush();
  console.log('device block ring: aborted prefix discarded, fragmented wire and complete END display passed');

  // A loaded page put into the back/forward cache must restart on restoration.
  const cached=fixture(); cached.ui.startStream(); await cached.run(0);
  const cachedReader=cached.response(cached.requests[0]); await flush(); cached.ui.accept();
  cached.events.pagehide();
  assert.equal(cached.ui.state().loaded,false,'stopped stream must clear the connected state');
  assert.equal(cached.requests[0].signal.aborted,true);
  assert.equal(typeof cached.events.pageshow,'function','cached pages need a restore handler');
  cached.events.pageshow({persisted:true}); await flush();
  assert.equal(cached.requests.length,1); await flush();
  assert.equal(cachedReader.cancelled,1); assert.equal(cachedReader.released,1);

  // A fetch resolved after cancellation cannot rewrite the replacement's format/UI.
  const race=fixture(); race.ui.startStream(); await race.run(0);
  race.element('streamRetryBtn').listeners.click(); await race.run(0);
  race.response(race.requests[1]); await flush(); race.ui.accept();
  race.response(race.requests[0],256); await flush();
  assert.equal(race.element('stream').width,384); assert.equal(race.ui.state().loaded,true);

  // EOF stops the stream; an aborted reader finishing later cannot cancel a
  // later manual reconnect.
  const ended=fixture(); ended.ui.startStream(); await ended.run(0);
  const reader=ended.response(ended.requests[0]); await flush(); ended.ui.accept();
  reader.pending.resolve({done:true}); await flush();
  assert.equal(ended.ui.state().loaded,false);
  ended.element('streamRetryBtn').listeners.click(); await ended.run(0);
  ended.response(ended.requests[1]); await flush(); ended.ui.accept();
  assert.equal(ended.ui.state().active,true);
  assert.equal(reader.cancelled,1); assert.equal(reader.released,1);

  // A stalled stream stops without an automatic retry; manual reconnect remains.
  const stalled=fixture(); stalled.ui.startStream(); await stalled.run(0);
  stalled.response(stalled.requests[0]); await flush(); stalled.ui.accept();
  stalled.advance(6000); stalled.ui.streamWatchdog();
  assert.equal(stalled.requests[0].signal.aborted,true);
  stalled.document.hidden=true; stalled.documentEvents.visibilitychange(); await flush();
  assert.equal(stalled.timers.size,0); assert.equal(stalled.ui.state().loaded,false);
  stalled.document.hidden=false; stalled.documentEvents.visibilitychange(); await flush();
  stalled.element('streamRetryBtn').listeners.click(); await stalled.run(0);
  assert.equal(stalled.requests.length,2);

  // Device unavailable: keep the stream stopped until the user retries.
  const offline=fixture(); offline.ui.startStream(); await offline.run(0);
  offline.requests[0].reject(new Error('USB network gone')); await flush();
  assert.equal(typeof offline.events.online,'function');
  offline.events.online(); await flush(); assert.equal(offline.requests.length,1);
  offline.element('streamRetryBtn').listeners.click(); await offline.run(0);
  assert.equal(offline.requests.length,2);
  offline.response(offline.requests[1]); await flush(); offline.ui.accept();
  offline.events.online(); assert.equal(offline.requests.length,2);
  console.log('device console cache restore/manual retry/stale fetch/EOF/stall/background/network recovery passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
