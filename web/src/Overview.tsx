import { useEffect, useState } from 'preact/hooks'
import type { ProbeState, Snapshot } from './live'
import type { JournalEntry } from './Journal'

type Page = 'calibration' | 'alerts' | 'journal' | 'system'
type Calibration = { temperature: { calibrated: boolean; timestamp: number | null; state: string; storageError: boolean } }
type Diagnostics = { ipAddress: string | null; wifiConnected: boolean; freeHeapBytes: number }
type OverviewData = { calibration: Calibration | null; events: JournalEntry[] | null; tips: string[] | null; diagnostics: Diagnostics | null }
const probeLabels: Record<ProbeState, string> = { connected: 'Connected', disconnected: 'Disconnected', unstable: 'Unstable', out_of_range: 'Out of range', disabled: 'Not enabled' }

export function Overview({ snapshot, online, timezone, navigate }: { snapshot: Snapshot | null; online: boolean; timezone: string; navigate: (page: Page) => void }) {
  const [data, setData] = useState<OverviewData>({ calibration: null, events: null, tips: null, diagnostics: null })
  useEffect(() => {
    if (!online) { setData({ calibration: null, events: null, tips: null, diagnostics: null }); return }
    const controller = new AbortController()
    let active = true
    let timer: ReturnType<typeof setTimeout>
    const get = async <T,>(url: string): Promise<T | null> => {
      try {
        const response = await fetch(url, { cache: 'no-store', signal: controller.signal })
        return response.ok ? await response.json() as T : null
      } catch { return null }
    }
    const load = async () => {
      const [calibration, events, tips, diagnostics] = await Promise.all([
        get<Calibration>('/api/calibration'), get<{ events: JournalEntry[] }>('/api/events'),
        get<{ tips: string[] }>('/api/tips'), get<Diagnostics>('/api/system'),
      ])
      if (active) {
        setData({ calibration, events: events?.events ?? null, tips: tips?.tips ?? null, diagnostics })
        timer = setTimeout(load, 30000)
      }
    }
    void load()
    return () => { active = false; controller.abort(); clearTimeout(timer) }
  }, [online])
  const temperature = data.calibration?.temperature
  const alert = snapshot?.alerts
  const alertText = !alert ? 'Status unavailable' : alert.settingsError ? 'Alert settings unavailable' : ({ normal: 'No active temperature alert', low: 'Temperature below minimum', high: 'Temperature above maximum', probe_fault: 'Temperature probe fault' })[alert.temperature]
  const date = (timestamp: number | null) => timestamp ? new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short', timeZone: timezone }).format(timestamp * 1000) : 'Time unavailable'
  return <>
    <section class="overview-panel overview-calibration"><div class="panel-heading"><div><h2>Calibration</h2><p>Probe status and calibration</p></div><button type="button" onClick={() => navigate('calibration')}>Manage <span aria-hidden="true">›</span></button></div>
      <div class="overview-list"><div><span>Temperature probe</span><strong>{!temperature ? 'Unavailable' : temperature.storageError ? 'Storage error' : temperature.calibrated ? 'Calibrated' : 'Factory offset'}</strong></div><div><span>Last calibration</span><strong>{temperature?.timestamp ? date(temperature.timestamp) : 'Not available'}</strong></div><div><span>pH probe</span><strong>{snapshot ? probeLabels[snapshot.ph.state] : 'Unavailable'}</strong></div><div><span>TDS probe</span><strong>{snapshot ? probeLabels[snapshot.tds.state] : 'Unavailable'}</strong></div></div>
    </section>
    <section class="overview-panel overview-advice"><div class="panel-heading"><div><h2>Aquarium notes</h2><p>Tips and recent activity</p></div><button type="button" onClick={() => navigate('journal')}>Journal <span aria-hidden="true">›</span></button></div>
      {data.tips?.length ? <p class="overview-tip">{data.tips[0]}</p> : <p class="overview-empty">{online ? 'No tips available.' : 'Device offline.'}</p>}
      {data.events?.slice(0, 2).map(event => <div class="overview-event" key={event.id}><span>{event.text}</span><small>{date(event.timestamp)}</small></div>)}
    </section>
    <section class="overview-panel overview-alerts"><div class="panel-heading"><div><h2>Alerts &amp; notifications</h2><p>Temperature alert status</p></div><button type="button" onClick={() => navigate('alerts')}>Manage <span aria-hidden="true">›</span></button></div>
      <div class="overview-status"><span class={`status-dot ${alert?.temperature === 'normal' && !alert.settingsError ? 'good' : ''}`} /><strong>{alertText}</strong></div>
      <p class="overview-empty">{data.events?.find(event => event.kind === 'alert' || event.kind === 'recovery')?.text ?? 'No recent alert events available.'}</p>
    </section>
    <section class="overview-panel overview-system"><div class="panel-heading"><div><h2>System &amp; device</h2><p>Connection and diagnostics</p></div><button type="button" onClick={() => navigate('system')}>Details <span aria-hidden="true">›</span></button></div>
      <div class="overview-list"><div><span>Network</span><strong>{!snapshot ? 'Unavailable' : snapshot.wifi === 'connected' ? 'Wi-Fi connected' : 'Setup network'}</strong></div><div><span>IP address</span><strong>{data.diagnostics?.ipAddress ?? '--'}</strong></div><div><span>Uptime</span><strong>{snapshot ? `${Math.floor(snapshot.uptimeSeconds / 60)} min` : '--'}</strong></div><div><span>Device time</span><strong>{!snapshot ? 'Unavailable' : snapshot.time?.synchronized ? snapshot.time.local ?? 'Synchronized' : 'Not synchronized'}</strong></div></div>
    </section>
  </>
}