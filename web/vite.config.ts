import preact from '@preact/preset-vite'
import { defineConfig } from 'vite'
import { readFileSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { gzipSync } from 'node:zlib'
import { createHash } from 'node:crypto'
import { Resvg } from '@resvg/resvg-js'
import sharp from 'sharp'

const sourceAssets = resolve(import.meta.dirname, '../assets')

export default defineConfig({
  publicDir: false,
  plugins: [preact(), {
    name: 'dashboard-assets-dev',
    configureServer(server) {
      server.middlewares.use((request, response, next) => {
        const name = request.url?.split('?')[0]
        if (name !== '/jumbo.jpg' && name !== '/logo-aquamax.svg') return next()
        response.setHeader('Content-Type', name.endsWith('.jpg') ? 'image/jpeg' : 'image/svg+xml')
        response.end(readFileSync(resolve(sourceAssets, name.slice(1))))
      })
    },
  }, {
    name: 'compress-dashboard',
    apply: 'build',
    async closeBundle() {
      const directory = resolve(import.meta.dirname, '../data')
      writeFileSync(resolve(directory, 'logo-aquamax.svg'), readFileSync(resolve(sourceAssets, 'logo-aquamax.svg')))
      let banner = await sharp(resolve(sourceAssets, 'jumbo.jpg')).resize({ width: 1600, withoutEnlargement: true }).jpeg({ quality: 72, mozjpeg: true }).toBuffer()
      if (gzipSync(banner).length > 262144) banner = await sharp(resolve(sourceAssets, 'jumbo.jpg')).resize({ width: 1200, withoutEnlargement: true }).jpeg({ quality: 55, mozjpeg: true }).toBuffer()
      if (gzipSync(banner).length > 262144) throw new Error('Optimized dashboard banner exceeds the device upload limit')
      writeFileSync(resolve(directory, 'jumbo.jpg'), banner)
      const icon = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><rect width="512" height="512" rx="88" fill="#183b38"/><path fill="#cbe79e" d="M256 91 103 405h66l32-72h110l32 72h66L256 91Zm0 123 34 71h-68l34-71Z"/></svg>'
      for (const size of [192, 512]) writeFileSync(resolve(directory, `icon-${size}.png`), new Resvg(icon, { fitTo: { mode: 'width', value: size } }).render().asPng())
      const manifest = { name: 'AquaMax', short_name: 'AquaMax', start_url: '/', scope: '/', display: 'standalone', background_color: '#f3f6f2', theme_color: '#183b38', icons: [192, 512].map(size => ({ src: `/icon-${size}.png`, sizes: `${size}x${size}`, type: 'image/png' })) }
      writeFileSync(resolve(directory, 'manifest.webmanifest'), JSON.stringify(manifest))
      const files = ['index.html', 'app.js', 'app.css', 'manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'logo-aquamax.svg', 'jumbo.jpg']
      const digest = createHash('sha256').update('offline-shell-v2')
      for (const file of files) digest.update(readFileSync(resolve(directory, file)))
      const hash = digest.digest('hex').slice(0, 16)
      const worker = `const name='aquamax-${hash}';const assets=['/','/app.js','/app.css','/manifest.webmanifest','/icon-192.png','/icon-512.png','/logo-aquamax.svg','/jumbo.jpg'];self.addEventListener('install',event=>event.waitUntil(caches.open(name).then(cache=>cache.addAll(assets)).then(()=>self.skipWaiting()).catch(async error=>{await caches.delete(name);throw error})));self.addEventListener('activate',event=>event.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(key=>key.startsWith('aquamax-')&&key!==name).map(key=>caches.delete(key)))).then(()=>self.clients.claim())));self.addEventListener('fetch',event=>{const url=new URL(event.request.url);if(event.request.method!=='GET'||url.origin!==self.location.origin)return;const path=url.pathname;if(event.request.mode==='navigate'&&(path==='/'||path==='/index.html')){event.respondWith(caches.open(name).then(cache=>cache.match('/')).then(response=>response||fetch(event.request)));return}if(!assets.includes(path)||path==='/')return;event.respondWith(caches.open(name).then(cache=>cache.match(path)).then(response=>response||fetch(event.request)))})`
      writeFileSync(resolve(directory, 'service-worker.js'), worker)
      for (const name of [...files, 'service-worker.js']) {
        const path = resolve(import.meta.dirname, '../data', name)
        writeFileSync(`${path}.gz`, gzipSync(readFileSync(path)))
      }
    },
  }],
  build: {
    outDir: '../data',
    emptyOutDir: false,
    assetsInlineLimit: file => file.endsWith('.woff2') ? true : undefined,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        assetFileNames: 'app.[ext]',
      },
    },
  },
})
