import preact from '@preact/preset-vite'
import { defineConfig } from 'vite'
import { readFileSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import { gzipSync } from 'node:zlib'

export default defineConfig({
  publicDir: false,
  plugins: [preact(), {
    name: 'compress-dashboard',
    apply: 'build',
    closeBundle() {
      for (const name of ['index.html', 'app.js', 'app.css']) {
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
