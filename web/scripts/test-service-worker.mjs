import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { runInNewContext } from 'node:vm'

const source = await readFile(new URL('../../data/service-worker.js', import.meta.url), 'utf8')
const listeners = new Map()
const entries = new Map()
const removed = []
const cache = {
  async addAll(paths) { for (const path of paths) entries.set(path, `cached:${path}`) },
  async match(path) { return entries.get(path) },
}
const caches = {
  async open() { return cache },
  async keys() { return ['aquamax-old', 'other-cache'] },
  async delete(name) { removed.push(name); return true },
}
const self = {
  location: { origin: 'https://aquamax.example' },
  addEventListener(type, handler) { listeners.set(type, handler) },
  async skipWaiting() {},
  clients: { async claim() {} },
}
runInNewContext(source, { self, caches, URL, fetch: async () => { throw new Error('offline') } })

let pending
listeners.get('install')({ waitUntil(promise) { pending = promise } })
await pending
for (const path of ['/', '/app.js', '/app.css', '/logo-aquamax.svg', '/jumbo.jpg']) assert(entries.has(path), `Missing cached ${path}`)
listeners.get('activate')({ waitUntil(promise) { pending = promise } })
await pending
assert.deepEqual(removed, ['aquamax-old'])

function request(path, mode = 'cors', method = 'GET') {
  let response
  listeners.get('fetch')({ request: { url: `https://aquamax.example${path}`, mode, method }, respondWith(value) { response = value } })
  return response
}
assert.equal(await request('/?offline=1', 'navigate'), 'cached:/')
assert.equal(await request('/jumbo.jpg'), 'cached:/jumbo.jpg')
assert.equal(request('/api/state'), undefined)
assert.equal(request('/setup', 'navigate'), undefined)
assert.equal(request('/api/calibration/temp', 'cors', 'POST'), undefined)
removed.length = 0
cache.addAll = async () => { throw new Error('incomplete download') }
listeners.get('install')({ waitUntil(promise) { pending = promise } })
await assert.rejects(pending, /incomplete download/)
assert.equal(removed.length, 1)
assert.notEqual(removed[0], 'aquamax-old')
console.log('Service worker caches the dashboard shell and leaves device APIs and setup network-only.')