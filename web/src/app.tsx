import { useEffect, useState } from 'preact/hooks'
import thermometer from '../../assets/icons/thermometer.svg?raw'
import thermometerOff from '../../assets/icons/thermometer-off.svg?raw'
import drop from '../../assets/icons/drop.svg?raw'
import dropOff from '../../assets/icons/drop-off.svg?raw'
import drops from '../../assets/icons/three-drops.svg?raw'
import dropsOff from '../../assets/icons/three-drops-off.svg?raw'
import { startLiveFeed } from './live'
import type { ProbeState, Snapshot } from './live'
import { History } from './History'
import { Calibration } from './Calibration'
import './app.css'

const stateLabels: Record<ProbeState, string> = {
  connected: 'Connected', disconnected: 'Probe disconnected', unstable: 'Unstable',
  out_of_range: 'Out of range', disabled: 'Not enabled',
}

const timezones = ['Europe/Paris', 'Europe/London', 'Etc/UTC', 'America/New_York', 'America/Los_Angeles', 'Asia/Tokyo', 'Australia/Sydney']

function Gauge({ name, unit, value, state, icon, mutedIcon, maximum }: {
  name: string; unit: string; value: number | null; state: ProbeState | null;
  icon: string; mutedIcon: string; maximum: number
}) {
  const active = state === 'connected' && value !== null && Number.isFinite(value)
  const angle = (135 + Math.max(0, Math.min(1, (value ?? 0) / maximum)) * 270) * Math.PI / 180
  const label = state === null ? 'Awaiting device' : state === 'connected' && !active ? 'Waiting for reading' : stateLabels[state]
  return (
    <article class={`metric ${active ? 'metric-active' : 'metric-muted'}`}>
      <div class="metric-heading"><span>{name}</span><span class="metric-number">{name === 'Temperature' ? '01' : name === 'pH' ? '02' : '03'}</span></div>
      <div class="gauge" role="img" aria-label={`${name}: ${active ? `${value!.toFixed(2)} ${unit}` : 'no reading'}, ${label}`}>
        <svg class="gauge-ring" viewBox="0 0 200 200" aria-hidden="true">
          <defs><linearGradient id="temperature-gradient" x1="0" y1="1" x2="1" y2="0"><stop stop-color="#3c91bd"/><stop offset="0.48" stop-color="#59c4b1"/><stop offset="0.75" stop-color="#e4b85b"/><stop offset="1" stop-color="#d96d58"/></linearGradient></defs>
          <path d="M 34.95 165.05 A 92 92 0 1 1 165.05 165.05" fill="none" stroke="currentColor" stroke-width="5" stroke-linecap="round" />
          {active && <><path d="M 34.95 165.05 A 92 92 0 1 1 165.05 165.05" fill="none" stroke="url(#temperature-gradient)" stroke-width="5" stroke-linecap="round"/><circle cx={100 + 92 * Math.cos(angle)} cy={100 + 92 * Math.sin(angle)} r="4" fill="#fff" /></>}
        </svg>
        <div class="gauge-content">
          <span class="probe-icon" dangerouslySetInnerHTML={{ __html: active ? icon : mutedIcon }} />
          <strong class="gauge-value">{active ? value!.toFixed(1) : '--'}</strong>
          <span class="gauge-unit">{unit}</span>
        </div>
      </div>
      <div class="metric-footer"><span class={`status-dot ${active ? 'good' : ''}`} /><span>{label}</span></div>
    </article>
  )
}

export function App() {
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null)
  const [status, setStatus] = useState<'loading' | 'online' | 'offline'>('loading')
  const [updated, setUpdated] = useState<string | null>(null)
  const [timezonePending, setTimezonePending] = useState<string | null>(null)
  const [timezoneError, setTimezoneError] = useState(false)
  const [page, setPage] = useState<'home' | 'history' | 'calibration'>('home')

  useEffect(() => {
    return startLiveFeed({
      socket: () => new WebSocket(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.hostname}:81/ws`),
      request: async signal => {
        const response = await fetch('/api/state', { cache: 'no-store', signal })
        if (!response.ok) throw new Error('Device unavailable')
        return response.json()
      },
      setTimer: (callback, delay) => window.setTimeout(callback, delay),
      clearTimer: timer => window.clearTimeout(timer),
      onSnapshot: data => {
        setSnapshot(data)
        setStatus('online')
        setUpdated(new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }))
      },
      onOffline: () => { setSnapshot(null); setStatus('offline') },
    })
  }, [])

  const changeTimezone = async (timezone: string) => {
    setTimezonePending(timezone)
    setTimezoneError(false)
    const controller = new AbortController()
    const timeout = window.setTimeout(() => controller.abort(), 6000)
    try {
      const response = await fetch('/api/timezone', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ timezone }),
        signal: controller.signal,
      })
      if (!response.ok) throw new Error('Timezone update failed')
      setSnapshot(current => current?.time ? { ...current, time: { ...current.time, timezone } } : current)
    } catch { setTimezoneError(true) }
    finally { window.clearTimeout(timeout); setTimezonePending(null) }
  }

  return (
    <div class="shell">
      <header class="topbar"><div class="brand"><span class="brand-mark" aria-hidden="true">A</span><span>AquaMax<small>WATER MONITOR</small></span></div><div class="connection" role="status"><span class={`status-dot ${status === 'online' ? 'good' : ''}`} />{status === 'loading' ? 'Connecting' : status === 'online' ? 'Device online' : 'Device offline'}</div></header>
      <nav class="main-nav" aria-label="Main navigation"><button type="button" aria-current={page === 'home' ? 'page' : undefined} onClick={() => setPage('home')}>Home</button><button type="button" aria-current={page === 'history' ? 'page' : undefined} onClick={() => setPage('history')}>History</button><button type="button" aria-current={page === 'calibration' ? 'page' : undefined} onClick={() => setPage('calibration')}>Calibration</button></nav>
      <main class="content">
        {page === 'home' ? <>
        <div class="page-heading"><div><div class="eyebrow">AQUARIUM / OVERVIEW</div><h1>Water conditions</h1><p>Live readings from your aquarium.</p></div><div class="update-time"><span>LAST UPDATE</span><strong>{status === 'online' ? updated : '--:--:--'}</strong></div></div>
        <div class="section-label"><h2>Live measurements</h2><span>DS18B20 + analog probes</span></div>
        <div class="metrics">
          <Gauge name="Temperature" unit="°C" value={snapshot?.temperature.valueC ?? null} state={snapshot?.temperature.state ?? null} icon={thermometer} mutedIcon={thermometerOff} maximum={40} />
          <Gauge name="pH" unit="pH" value={snapshot?.ph.value ?? null} state={snapshot?.ph.state ?? null} icon={drop} mutedIcon={dropOff} maximum={14} />
          <Gauge name="TDS" unit="ppm" value={snapshot?.tds.value ?? null} state={snapshot?.tds.state ?? null} icon={drops} mutedIcon={dropsOff} maximum={1000} />
        </div>
        <div class="device-row"><div><span class="device-label">CONNECTION</span><strong>{status === 'online' ? snapshot?.wifi === 'connected' ? 'Wi-Fi connected' : 'Setup network' : 'Waiting for device'}</strong></div><div><span class="device-label">DEVICE UPTIME</span><strong>{snapshot ? `${Math.floor(snapshot.uptimeSeconds / 60)} min` : '--'}</strong></div><div><span class="device-label">DEVICE TIME</span><strong>{snapshot?.time?.synchronized ? snapshot.time.local ?? 'Synchronized' : 'Not synchronized'}</strong></div><div><label class="device-label" for="timezone">TIMEZONE</label><select id="timezone" value={timezonePending ?? snapshot?.time?.timezone ?? 'Europe/Paris'} disabled={status !== 'online' || !snapshot?.time?.timezone || timezonePending !== null} onChange={event => void changeTimezone(event.currentTarget.value)}>{timezones.map(timezone => <option value={timezone} key={timezone}>{timezone.replaceAll('_', ' ')}</option>)}</select>{timezoneError && <span class="timezone-error" role="alert">Could not save timezone</span>}</div></div>
        </> : page === 'history' ? <History timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} /> : <Calibration timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} />}
      </main>
      <footer>AquaMax <span>LOCAL MONITORING</span></footer>
    </div>
  )
}