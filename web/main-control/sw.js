const CACHE='aquabeacon-main-control-20261025-v9';
self.addEventListener('install',event=>event.waitUntil(caches.open(CACHE).then(cache=>cache.addAll(['./','./index.html'])).then(()=>self.skipWaiting())));
self.addEventListener('activate',event=>event.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(key=>key.startsWith('aquabeacon-main-control-')&&key!==CACHE).map(key=>caches.delete(key)))).then(()=>self.clients.claim())));
self.addEventListener('fetch',event=>{
 const url=new URL(event.request.url);
 if(event.request.method!=='GET'||url.origin!==self.location.origin||!url.pathname.startsWith(new URL('./',self.location).pathname)||url.pathname.endsWith('/sw.js'))return;
 // Refresh online; retain a bounded wait and an offline fallback for the Pico AP.
 event.respondWith((async()=>{
  const cache=await caches.open(CACHE);
  try{
   const response=await fetch(event.request,{signal:AbortSignal.timeout(4000)});
   if(!response.ok)throw Error('HTTP '+response.status);
   const copy=response.clone();event.waitUntil(cache.put(event.request,copy).catch(()=>{}));
   return response;
  }catch(error){return await cache.match(event.request)||await cache.match('./index.html')||Response.error();}
 })());
});
