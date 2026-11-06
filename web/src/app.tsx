import { useEffect, useRef, useState } from 'preact/hooks'
import { Settings as SettingsIcon, TriangleAlert, CircleX } from 'lucide-preact'
import thermometer from '../../assets/icons/thermometer.svg?raw'
import thermometerOff from '../../assets/icons/thermometer-off.svg?raw'
import drop from '../../assets/icons/drop.svg?raw'
import dropOff from '../../assets/icons/drop-off.svg?raw'
import drops from '../../assets/icons/three-drops.svg?raw'
import dropsOff from '../../assets/icons/three-drops-off.svg?raw'
import { startLiveFeed } from './live'
import { phCalibrationStatus } from './live'
import type { ProbeState, ProbeWarning, Snapshot } from './live'
import { History } from './History'
import { Calibration } from './Calibration'
import { Alerts } from './Alerts'
import { Journal } from './Journal'
import { Settings, System } from './System'
import { Overview } from './Overview'
import { SEGS } from './GaugeArc'
import './app.css'

const SEGMENTS = 90;
const STROKE_WIDTH = 5;
const R = 92;

const stateLabels: Record<ProbeState, string> = {
  connected: 'Connected', disconnected: 'Probe disconnected', unstable: 'Unstable',
  out_of_range: 'Out of range', disabled: 'Not enabled', signal_fault: 'Check signal or connection', waiting: 'Waiting for measurement',
}
const warningLabels: Record<Exclude<ProbeWarning, 'normal'>, string> = { signal_fault: 'Check signal or connection', out_of_range: 'Measurement out of range', unstable: 'Measurement unstable' }

const timezones = ['Europe/Paris', 'Europe/London', 'Etc/UTC', 'America/New_York', 'America/Los_Angeles', 'Asia/Tokyo', 'Australia/Sydney']
type ThemePreference = 'system' | 'light' | 'dark'

function initialTheme(): ThemePreference {
  try {
    const saved = localStorage.getItem('aquamax-theme')
    return saved === 'light' || saved === 'dark' ? saved : 'system'
  } catch { return 'system' }
}

function Gauge({ name, unit, value, state, icon, mutedIcon, maximum, quality, missingLabel }: {
  name: string; unit: string; value: number | null; state: ProbeState | null;
  icon: string; mutedIcon: string; maximum: number; quality?: string; missingLabel?: string
}) {
  const active = state === 'connected' && value !== null && Number.isFinite(value)
  const angle = (135 + Math.max(0, Math.min(1, (value ?? 0) / maximum)) * 270) * Math.PI / 180
  const label = active && quality ? quality : state === null ? 'Awaiting device' : state === 'connected' && !active ? missingLabel ?? 'Waiting for reading' : state === 'disconnected' && name !== 'Temperature' ? 'Check signal or connection' : stateLabels[state]
  const v = Math.min(1, Math.max(0, value ?? 0));
const visible = Math.round(v * SEGMENTS);
  return (
    <article class={`metric ${active ? 'metric-active' : 'metric-muted'}`}>
      <div class="metric-heading"><span>{name}</span><span class="metric-number">{name === 'Temperature' ? '01' : name === 'pH' ? '02' : '03'}</span></div>
      <div class="gauge" role="img" aria-label={`${name}: ${active ? `${value!.toFixed(2)} ${unit}` : 'no reading'}, ${label}`}>
      <svg className="gauge-ring" viewBox="0 0 200 200" aria-hidden="true">
      {/* gray track */}
      <path
      style={{ strokeWidth: STROKE_WIDTH }} 
        d="M 34.95 165.05 A 92 92 0 1 1 165.05 165.05"
        fill="none"
        stroke="currentColor"
        strokeWidth={STROKE_WIDTH}
        strokeLinecap="round"
      />
      {active && (
        <>
          {SEGS.slice(0, visible).map((s, i) => (
            <path key={i} d={s.d} stroke={s.color} strokeWidth={STROKE_WIDTH} fill="none" style={{ strokeWidth: STROKE_WIDTH }}  />
          ))}
          <circle
            cx={100 + R * Math.cos(angle)}
            cy={100 + R * Math.sin(angle)}
            r={STROKE_WIDTH + 2}
            fill={document.documentElement.dataset.theme === 'light' ? '#17283C' : '#F2F6FC'}
          />
        </>
      )}
    </svg>
        <div class="gauge-content">
          {state === 'out_of_range' ? <span class="probe-icon probe-range"><TriangleAlert aria-hidden="true" /></span> : state === 'signal_fault' ? <span class="probe-icon probe-fault"><CircleX aria-hidden="true" /></span> : <span class="probe-icon" dangerouslySetInnerHTML={{ __html: active ? icon : mutedIcon }} />}
          <strong class="gauge-value">{active ? value!.toFixed(1) : '--'}</strong>
          <span class="gauge-unit">{unit}</span>
        </div>
      </div>
      <div class="metric-footer"><span class={`status-dot ${active ? 'good' : ''}`} /><span>{label}</span></div>
    </article>
  )
}

export function App() {
  const [theme, setTheme] = useState<ThemePreference>(initialTheme)
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null)
  const [status, setStatus] = useState<'loading' | 'online' | 'offline'>('loading')
  const [updated, setUpdated] = useState<string | null>(null)
  const [timezonePending, setTimezonePending] = useState<string | null>(null)
  const [timezoneError, setTimezoneError] = useState(false)
  const [page, setPage] = useState<'home' | 'history' | 'calibration' | 'alerts' | 'journal' | 'settings' | 'system'>('home')
  const [browserEnabled, setBrowserEnabled] = useState(() => {
    try { return window.isSecureContext && typeof Notification !== 'undefined' && Notification.permission === 'granted' && localStorage.getItem('aquamax-notifications') === 'enabled' } catch { return false }
  })
  const browserEnabledRef = useRef(browserEnabled)
  browserEnabledRef.current = browserEnabled
  const lastAlertSequence = useRef<number | null>(null)
  const lastProbeWarnings = useRef<Partial<Record<'ph' | 'tds', ProbeWarning>>>({})

  useEffect(() => {
    const media = window.matchMedia('(prefers-color-scheme: dark)')
    const apply = () => { document.documentElement.dataset.theme = theme === 'system' ? (media.matches ? 'dark' : 'light') : theme }
    apply()
    media.addEventListener('change', apply)
    try { localStorage.setItem('aquamax-theme', theme) } catch { }
    return () => media.removeEventListener('change', apply)
  }, [theme])

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
        if (data.alerts) {
          const previous = lastAlertSequence.current
          const event = data.alerts.event
          if (previous !== null && data.alerts.sequence > previous && browserEnabledRef.current && event !== 'none') {
            const messages = { low: 'Temperature below minimum', high: 'Temperature above maximum', probe_fault: 'Temperature probe fault', recovered: 'Temperature alert recovered' }
            try { new Notification('AquaMax', { body: messages[event], tag: `aquamax-${data.alerts.sequence}` }) } catch { }
          }
          lastAlertSequence.current = data.alerts.sequence
          for (const probe of ['ph', 'tds'] as const) {
            const warning = data.alerts[probe]
            if (!warning) continue
            if (data[probe].state === 'disabled') { lastProbeWarnings.current[probe] = 'normal'; continue }
            if (data[probe].state === 'waiting') continue
            const previousWarning = lastProbeWarnings.current[probe]
            if (previousWarning !== undefined && previousWarning !== warning && warning !== 'normal' && browserEnabledRef.current) {
              try { new Notification('AquaMax', { body: `${probe === 'ph' ? 'pH' : 'TDS'}: ${warningLabels[warning]}`, tag: `aquamax-${probe}` }) } catch { }
            }
            lastProbeWarnings.current[probe] = warning
          }
        }
        setSnapshot(data)
        setStatus('online')
        setUpdated(new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }))
      },
      onOffline: () => { setSnapshot(null); setStatus('offline') },
    })
  }, [])

  const changeBrowserNotifications = (enabled: boolean) => {
    setBrowserEnabled(enabled)
    try { localStorage.setItem('aquamax-notifications', enabled ? 'enabled' : 'disabled') } catch { }
  }

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
      <header class="topbar"><button type="button" class="brand" onClick={() => setPage('home')} aria-label="AquaMax home"><img src="/logo-aquamax.svg" alt="AquaMax" /></button><div class="topbar-right"><span class="tank-name">AquaMax <span>/</span> Aquarium</span><div class="connection" role="status"><span class={`status-dot ${status === 'online' ? 'good' : ''}`} />{status === 'loading' ? 'Connecting' : status === 'online' ? 'Online' : 'Offline'}</div><button class="settings-link" type="button" onClick={() => setPage('settings')} aria-label="Settings" title="Settings"><SettingsIcon size={17} aria-hidden="true" /></button></div></header>
      <nav class="main-nav" aria-label="Main navigation"><button type="button" aria-current={page === 'home' ? 'page' : undefined} onClick={() => setPage('home')}>Home</button><button type="button" aria-current={page === 'history' ? 'page' : undefined} onClick={() => setPage('history')}>History</button><button type="button" aria-current={page === 'calibration' ? 'page' : undefined} onClick={() => setPage('calibration')}>Calibration</button><button type="button" aria-current={page === 'alerts' ? 'page' : undefined} onClick={() => setPage('alerts')}>Alerts</button><button type="button" aria-current={page === 'journal' ? 'page' : undefined} onClick={() => setPage('journal')}>Journal</button><button type="button" aria-current={page === 'settings' ? 'page' : undefined} onClick={() => setPage('settings')}>Settings</button><button type="button" aria-current={page === 'system' ? 'page' : undefined} onClick={() => setPage('system')}>System</button></nav>
      <main class="content">
        {page === 'home' ? <>
        <div class="dashboard-hero"><div><span class="hero-eyebrow">YOUR AQUARIUM, AT A GLANCE</span><h1>Welcome back</h1><div class="hero-status"><span class={`status-dot ${status === 'online' ? 'good' : ''}`} />{status === 'online' ? 'Monitoring live' : status === 'loading' ? 'Connecting to device' : 'Device offline'} <span class="hero-updated">{status === 'online' && updated ? `Updated ${updated}` : 'Waiting for readings'}</span></div></div><p>A healthier aquarium.<br />A brighter tomorrow.</p></div>
        {snapshot?.alerts?.temperature && snapshot.alerts.temperature !== 'normal' && <div class="active-alert" role="alert"><strong>Temperature alert</strong><span>{snapshot.alerts.temperature === 'low' ? 'Below minimum' : snapshot.alerts.temperature === 'high' ? 'Above maximum' : 'Probe fault'}</span><button type="button" onClick={() => setPage('alerts')}>View alerts</button></div>}
        {(['ph', 'tds'] as const).map(probe => { const warning = snapshot?.alerts?.[probe]; return warning && warning !== 'normal' ? <div class="active-alert" role="alert" key={probe}><strong>{probe === 'ph' ? 'pH' : 'TDS'} warning</strong><span>{warningLabels[warning]}</span><button type="button" onClick={() => setPage('alerts')}>View alerts</button></div> : null })}
        {snapshot?.alerts?.settingsError && <p class="calibration-error" role="alert">Alert settings could not be loaded. Temperature thresholds are disabled.</p>}
        <div class="overview-grid"><section class="overview-panel overview-readings"><div class="panel-heading"><div><h2>Live readings</h2><p>Real-time water parameters</p></div><span class="live-label"><span class={`status-dot ${status === 'online' ? 'good' : ''}`} /> {status === 'online' ? 'LIVE' : status === 'loading' ? 'CONNECTING' : 'OFFLINE'}</span></div><div class="metrics">
          <Gauge name="Temperature" unit="°C" value={snapshot?.temperature.valueC ?? null} state={snapshot?.temperature.state ?? null} icon={thermometer} mutedIcon={thermometerOff} maximum={40} />
          <Gauge name="pH" unit="pH" value={snapshot?.ph.value ?? null} state={snapshot?.ph.state ?? null} icon={drop} mutedIcon={dropOff} maximum={14} missingLabel={phCalibrationStatus(snapshot?.ph)} />
          <Gauge name="TDS" unit="ppm" value={snapshot?.tds.value ?? null} state={snapshot?.tds.state ?? null} icon={drops} mutedIcon={dropsOff} maximum={1000} quality={snapshot?.tds.calibrated === false ? (snapshot.tds.defaultTemperature ? 'Estimate · 25°C assumed' : 'Estimate') : snapshot?.tds.defaultTemperature ? '25°C assumed' : undefined} />
        </div></section><section class="overview-panel overview-history"><div class="panel-heading"><div><h2>History</h2><p>Water parameters over time</p></div><button type="button" onClick={() => setPage('history')}>View all <span aria-hidden="true">›</span></button></div>{status === 'online' ? <History timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} compact /> : <p class="history-placeholder">{status === 'loading' ? 'Connecting to device...' : 'History unavailable while offline.'}</p>}</section><Overview snapshot={snapshot} online={status === 'online'} timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} navigate={setPage} /></div>
        </> : page === 'history' ? <History timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} /> : page === 'calibration' ? <Calibration timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} /> : page === 'alerts' ? <Alerts live={snapshot?.alerts} snapshot={snapshot} browserEnabled={browserEnabled} onBrowserChange={changeBrowserNotifications} /> : page === 'journal' ? <Journal timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} /> : page === 'settings' ? <Settings timezone={snapshot?.time?.timezone ?? 'Europe/Paris'} pending={timezonePending} error={timezoneError} online={status === 'online'} timezones={timezones} onTimezone={zone => void changeTimezone(zone)} theme={theme} onTheme={setTheme} /> : <System />}
      </main>
      <footer>AquaMax <span>LOCAL MONITORING</span></footer>
    </div>
  )
}