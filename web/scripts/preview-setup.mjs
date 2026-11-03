import { createServer } from 'node:http'
import { readFile } from 'node:fs/promises'

const root = new URL('../../', import.meta.url)
const header = await readFile(new URL('include/webserver/SetupPage.h', root), 'utf8')
const page = header.match(/R"SETUP\(([\s\S]*?)\)SETUP"/)?.[1]?.replace('__SETUP_TOKEN__', 'preview-token')
if (!page) throw new Error('Setup page not found')
let state = 'idle'
const networks = [{ ssid: 'Home Wi-Fi', rssi: -48, secure: true }, { ssid: 'AquaMax <Lab>', rssi: -70, secure: false }]

createServer(async (request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1')
  if ((url.pathname === '/' || url.pathname === '/setup') && request.method === 'GET') {
    response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }).end(page)
  } else if (url.pathname === '/setup/networks') {
    response.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ state: 'ready', networks }))
  } else if (url.pathname === '/setup' && request.method === 'POST') {
    let body = ''
    for await (const chunk of request) body += chunk
    const form = new URLSearchParams(body)
    state = form.get('ssid') === 'Fail network' ? 'failed' : 'connected'
    response.writeHead(202, { 'Content-Type': 'application/json' }).end('{"ok":true}')
  } else if (url.pathname === '/setup/status') {
    response.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ state, ip: state === 'connected' ? '192.168.1.42' : '', reason: state === 'failed' ? 202 : 0 }))
  } else if (['/app.css', '/logo-aquamax.svg', '/jumbo.jpg'].includes(url.pathname)) {
    try {
      const data = await readFile(new URL(`data${url.pathname}`, root))
      response.writeHead(200, { 'Content-Type': url.pathname.endsWith('.css') ? 'text/css' : url.pathname.endsWith('.svg') ? 'image/svg+xml' : 'image/jpeg' }).end(data)
    } catch { response.writeHead(404).end() }
  } else response.writeHead(404).end()
}).listen(4174, '127.0.0.1', () => console.log('Setup preview (mock data): http://127.0.0.1:4174/setup'))