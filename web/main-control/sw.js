const CACHE='aquabeacon-main-control-v4';
self.addEventListener('install',event=>event.waitUntil(caches.open(CACHE).then(cache=>cache.addAll(['./','./index.html'])).then(()=>self.skipWaiting())));
self.addEventListener('activate',event=>event.waitUntil(self.clients.claim()));
self.addEventListener('fetch',event=>{
 const url=new URL(event.request.url);
 if(event.request.method!=='GET'||url.origin!==self.location.origin||!url.pathname.startsWith(new URL('./',self.location).pathname))return;
 const update=fetch(event.request).then(response=>{if(response.ok){const copy=response.clone();return caches.open(CACHE).then(cache=>cache.put(event.request,copy)).then(()=>response)}return response});
 event.waitUntil(update.catch(()=>{}));
 event.respondWith(caches.open(CACHE).then(cache=>cache.match(event.request)).then(cached=>cached||update).catch(()=>caches.open(CACHE).then(cache=>cache.match('./index.html'))));
});
