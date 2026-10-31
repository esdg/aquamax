import preact from '@preact/preset-vite'
import { defineConfig } from 'vite'
import { readFileSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { gzipSync } from 'node:zlib'
import { createHash } from 'node:crypto'
import { Resvg } from '@resvg/resvg-js'

export default defineConfig({
  publicDir: false,
  plugins: [preact(), {
    name: 'compress-dashboard',
    apply: 'build',
    closeBundle() {
      const directory = resolve(import.meta.dirname, '../data')
      const icon = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><rect width="512" height="512" rx="88" fill="#183b38"/><path fill="#cbe79e" d="M256 91 103 405h66l32-72h110l32 72h66L256 91Zm0 123 34 71h-68l34-71Z"/></svg>'
      for (const size of [192, 512]) writeFileSync(resolve(directory, `icon-${size}.png`), new Resvg(icon, { fitTo: { mode: 'width', value: size } }).render().asPng())
      const manifest = { name: 'AquaMax', short_name: 'AquaMax', start_url: '/', scope: '/', display: 'standalone', background_color: '#f3f6f2', theme_color: '#183b38', icons: [192, 512].map(size => ({ src: `/icon-${size}.png`, sizes: `${size}x${size}`, type: 'image/png' })) }
      writeFileSync(resolve(directory, 'manifest.webmanifest'), JSON.stringify(manifest))
      const hash = createHash('sha256').update(readFileSync(resolve(directory, 'app.js'))).update(readFileSync(resolve(directory, 'app.css'))).digest('hex').slice(0, 16)
      const worker = `const name='aquamax-${hash}';const assets=['/','/app.js','/app.css','/manifest.webmanifest','/icon-192.png','/icon-512.png'];self.addEventListener('install',event=>event.waitUntil(caches.open(name).then(cache=>cache.addAll(assets)).then(()=>self.skipWaiting())));self.addEventListener('activate',event=>event.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(key=>key.startsWith('aquamax-')&&key!==name).map(key=>caches.delete(key)))).then(()=>self.clients.claim())));self.addEventListener('fetch',event=>{const url=new URL(event.request.url);if(event.request.method!=='GET'||url.origin!==self.location.origin||!assets.includes(url.pathname))return;event.respondWith(fetch(event.request).catch(()=>caches.match(event.request)))})`
      writeFileSync(resolve(directory, 'service-worker.js'), worker)
      for (const name of ['index.html', 'app.js', 'app.css', 'manifest.webmanifest', 'service-worker.js', 'icon-192.png', 'icon-512.png']) {
        const path = resolve(import.meta.dirname, '../data', name)
        writeFileSync(`${path}.gz`, gzipSync(readFileSync(path)))
      }
    },
  }],
  build: {
    outDir: '../data',
    emptyOutDir: false,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        assetFileNames: 'app.[ext]',
      },
    },
  },
})
