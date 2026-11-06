import assert from 'node:assert/strict'
import { test } from 'node:test'
import { isSnapshot, phCalibrationStatus, startLiveFeed } from '../src/live.ts'

const snapshot = valueC => ({
  temperature: { state: 'connected', valueC },
  ph: { state: 'disabled', value: null },
  tds: { state: 'disabled', value: null },
  wifi: 'connected',
  uptimeSeconds: 100,
})

function fixture(request) {
  let now = 0
  let nextTimer = 1
  const timers = new Map()
  const sockets = []
  const received = []
  let offline = 0
  const stop = startLiveFeed({
    socket: () => {
      const socket = {
        close() { this.onclose?.() },
        message(value) { this.onmessage?.({ data: JSON.stringify(value) }) },
      }
      sockets.push(socket)
      return socket
    },
    request,
    setTimer: (callback, delay) => {
      const id = nextTimer++
      timers.set(id, { callback, at: now + delay })
      return id
    },
    clearTimer: timer => timers.delete(timer),
    onSnapshot: value => received.push(value),
    onOffline: () => { offline++ },
  })
  return {
    sockets, received, stop,
    offline: () => offline,
    advance(milliseconds) {
      const until = now + milliseconds
      for (;;) {
        const due = [...timers.entries()].filter(([, timer]) => timer.at <= until).sort((left, right) => left[1].at - right[1].at)[0]
        if (!due) break
        now = due[1].at
        timers.delete(due[0])
        due[1].callback()
      }
      now = until
    },
  }
}

async function settle() {
  for (let count = 0; count < 4; count++) await Promise.resolve()
}

test('rejects malformed or non-finite readings', () => {
  assert.equal(isSnapshot(snapshot(24)), true)
  assert.equal(isSnapshot(snapshot(NaN)), false)
  assert.equal(isSnapshot({ ...snapshot(24), tds: { state: 'unknown', value: 3 } }), false)
  assert.equal(isSnapshot({ ...snapshot(24), ph: { state: 'connected', value: null, calibrated: false, calibrationError: false } }), true)
  assert.equal(isSnapshot({ ...snapshot(24), ph: { state: 'connected', value: null, calibrated: 'false' } }), false)
  assert.equal(isSnapshot({ ...snapshot(24), ph: { state: 'connected', value: null, calibrationError: 'false' } }), false)
  const alerts = { temperature: 'normal', event: 'none', sequence: 0, settingsError: false }
  assert.equal(isSnapshot({ ...snapshot(24), alerts }), true)
  assert.equal(isSnapshot({ ...snapshot(24), alerts: { ...alerts, sequence: NaN } }), false)
  assert.equal(isSnapshot({ ...snapshot(24), alerts: { ...alerts, event: 'unexpected' } }), false)
  assert.equal(isSnapshot({ ...snapshot(24), alerts: { ...alerts, settingsError: 'false' } }), false)
})

test('stream wins over a late HTTP response and falls back on disconnect', async () => {
  let resolveFirst
  let calls = 0
  const feed = fixture(() => ++calls === 1 ? new Promise(resolve => { resolveFirst = resolve }) : Promise.resolve(snapshot(23)))
  feed.sockets[0].message(snapshot(25))
  resolveFirst(snapshot(20))
  await settle()
  assert.deepEqual(feed.received.map(item => item.temperature.valueC), [25])
  feed.sockets[0].close()
  await settle()
  assert.deepEqual(feed.received.map(item => item.temperature.valueC), [25, 23])
  feed.advance(3000)
  assert.equal(feed.sockets.length, 2)
  feed.sockets[1].message(snapshot(26))
  assert.equal(feed.received.at(-1).temperature.valueC, 26)
  feed.stop()
})

test('offline requests and a silent socket recover without stale readings', async () => {
  const feed = fixture(() => Promise.reject(new Error('offline')))
  await settle()
  assert.equal(feed.offline(), 1)
  feed.advance(6000)
  await settle()
  assert.equal(feed.sockets.length, 1)
  feed.advance(3000)
  assert.equal(feed.sockets.length, 2)
  feed.sockets[1].message(snapshot(24))
  assert.equal(feed.received.at(-1).temperature.valueC, 24)
  feed.stop()
})

test('pH calibration status does not hide signal faults or ordinary waiting', () => {
  assert.equal(phCalibrationStatus({ state: 'connected', value: null, calibrated: false }), 'Calibration required')
  assert.equal(phCalibrationStatus({ state: 'connected', value: null, calibrated: false, calibrationError: true }), 'Calibration storage error')
  assert.equal(phCalibrationStatus({ state: 'connected', value: null, calibrated: true }), undefined)
  assert.equal(phCalibrationStatus({ state: 'connected', value: null }), undefined)
  assert.equal(phCalibrationStatus({ state: 'connected', value: 7, calibrated: true }), undefined)
  assert.equal(phCalibrationStatus({ state: 'signal_fault', value: null, calibrated: false }), undefined)
  assert.equal(phCalibrationStatus({ state: 'waiting', value: null, calibrated: false }), undefined)
  assert.equal(phCalibrationStatus({ state: 'disabled', value: null, calibrated: false }), undefined)
})