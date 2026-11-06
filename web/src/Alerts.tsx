import { useEffect, useState } from 'preact/hooks'
import type { AlertStatus, Snapshot } from './live'

type Settings = { thresholdsEnabled: boolean; probeFaultEnabled: boolean; minimum: number; maximum: number; hysteresis: number; debounceSeconds: number; repeatSeconds: number; state: string }
type AlertData = { temperature: Settings; settingsError: boolean; ph: string; tds: string }
const presets = [
  { name: 'Custom', minimum: null, maximum: null },
  { name: 'Freshwater community', minimum: 23, maximum: 27 },
  { name: 'Shrimp', minimum: 20, maximum: 25 },
  { name: 'Planted', minimum: 22, maximum: 27 },
  { name: 'Discus', minimum: 28, maximum: 30 },
]
const alertLabels: Record<string, string> = { normal: 'No active alert', low: 'Temperature below minimum', high: 'Temperature above maximum', probe_fault: 'Temperature probe fault' }

export function Alerts({ live, snapshot, browserEnabled, onBrowserChange }: { live: AlertStatus | undefined; snapshot: Snapshot | null; browserEnabled: boolean; onBrowserChange: (enabled: boolean) => void }) {
  const [settings, setSettings] = useState<Settings | null>(null)
  const [preset, setPreset] = useState('Custom')
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')
  const [busy, setBusy] = useState(false)
  const [storageError, setStorageError] = useState(false)
  useEffect(() => {
    const controller = new AbortController()
    void fetch('/api/alerts', { cache: 'no-store', signal: controller.signal }).then(response => {
      if (!response.ok) throw new Error('Alert settings unavailable')
      return response.json() as Promise<AlertData>
    }).then(data => { setSettings(data.temperature); setStorageError(data.settingsError) }).catch(() => { if (!controller.signal.aborted) setError('Alert settings unavailable') })
    return () => controller.abort()
  }, [])
  const change = <Key extends keyof Settings>(key: Key, value: Settings[Key]) => setSettings(current => current ? { ...current, [key]: value } : current)
  const save = async () => {
    if (!settings) return
    setBusy(true); setError(''); setNotice('')
    try {
      const body = new URLSearchParams({ thresholdsEnabled: settings.thresholdsEnabled ? '1' : '0', probeFaultEnabled: settings.probeFaultEnabled ? '1' : '0', minimum: String(settings.minimum), maximum: String(settings.maximum), hysteresis: String(settings.hysteresis), debounceSeconds: String(settings.debounceSeconds), repeatSeconds: String(settings.repeatSeconds) })
      const response = await fetch('/api/alerts', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })
      if (!response.ok) throw new Error(await response.text())
      setStorageError(false); setNotice('Alert settings saved.')
    } catch (reason) { setError(reason instanceof Error ? reason.message : 'Could not save alerts') }
    finally { setBusy(false) }
  }
  const notificationAvailable = typeof Notification !== 'undefined' && window.isSecureContext
  const toggleNotifications = async () => {
    if (browserEnabled) { onBrowserChange(false); return }
    try {
      const permission = await Notification.requestPermission()
      if (permission === 'granted') onBrowserChange(true)
      else setError('Browser notifications were not permitted.')
    } catch { setError('Browser notifications are unavailable.') }
  }
  const testNotification = () => {
    try { new Notification('AquaMax', { body: 'Test notification' }) }
    catch { setError('Could not show a browser notification.') }
  }
  return <section>
    <div class="history-heading"><div><div class="eyebrow">AQUARIUM / ALERTS</div><h1>Alert settings</h1></div></div>
    <div class="alerts-layout"><div>
      <div class="alert-current"><span class="device-label">CURRENT STATUS</span><strong>{alertLabels[live?.temperature ?? settings?.state ?? 'normal'] ?? 'Unknown'}</strong></div>
      {storageError && <p class="calibration-error" role="alert">Saved alert settings could not be read. Temperature limits are disabled.</p>}
      {!settings ? <p class="history-placeholder">{error || 'Loading alert settings...'}</p> : <form class="alert-form" onSubmit={event => { event.preventDefault(); void save() }}>
        <div class="alert-form-heading"><div><span class="device-label">TEMPERATURE</span><h2>Thresholds</h2></div><label class="check-label"><input type="checkbox" checked={settings.thresholdsEnabled} onChange={event => change('thresholdsEnabled', event.currentTarget.checked)} /> Enabled</label></div>
        <label class="field-label" for="alert-profile">Starting profile</label><select id="alert-profile" value={preset} onChange={event => { const name = event.currentTarget.value; setPreset(name); const selected = presets.find(item => item.name === name); if (selected?.minimum != null && selected.maximum != null) setSettings(current => current ? { ...current, minimum: selected.minimum!, maximum: selected.maximum! } : current) }}>{presets.map(item => <option value={item.name}>{item.name}</option>)}</select>
        <small>Check the range against your aquarium's inhabitants before enabling it.</small>
        <div class="alert-fields"><label>Minimum (°C)<input type="number" step="0.1" min="-55" max="125" required value={settings.minimum} onInput={event => { setPreset('Custom'); change('minimum', event.currentTarget.valueAsNumber) }} /></label><label>Maximum (°C)<input type="number" step="0.1" min="-55" max="125" required value={settings.maximum} onInput={event => { setPreset('Custom'); change('maximum', event.currentTarget.valueAsNumber) }} /></label><label>Hysteresis (°C)<input type="number" step="0.1" min="0" required value={settings.hysteresis} onInput={event => change('hysteresis', event.currentTarget.valueAsNumber)} /></label><label>Debounce (seconds)<input type="number" step="1" min="0" max="3600" required value={settings.debounceSeconds} onInput={event => change('debounceSeconds', event.currentTarget.valueAsNumber)} /></label><label>Repeat (seconds)<input type="number" step="1" min="60" max="86400" required value={settings.repeatSeconds} onInput={event => change('repeatSeconds', event.currentTarget.valueAsNumber)} /></label></div>
        <label class="check-label fault-check"><input type="checkbox" checked={settings.probeFaultEnabled} onChange={event => change('probeFaultEnabled', event.currentTarget.checked)} /> Alert on probe fault</label>
        <div class="calibration-actions"><button class="calibration-primary" type="submit" disabled={busy}>Save settings</button></div>
      </form>}
      {error && settings && <p class="calibration-error" role="alert">{error}</p>}{notice && <p class="calibration-notice" role="status">{notice}</p>}
    </div><aside class="probe-health"><span class="device-label">NOTIFICATIONS</span><h2>Browser</h2><p class="calibration-instruction">Alerts appear while this dashboard is open.</p><div class="notification-actions"><button class="notification-button" type="button" disabled={!notificationAvailable} onClick={() => void toggleNotifications()}>{browserEnabled ? 'Disable notifications' : 'Enable notifications'}</button><button class="notification-button" type="button" disabled={!browserEnabled} onClick={testNotification}>Send test</button></div>{!notificationAvailable && <small class="notification-hint">Browser notifications require a supported secure origin.</small>}{(['ph', 'tds'] as const).map(probe => { const warning = live?.[probe]; return <div class="health-row" key={probe}><div><strong>{probe === 'ph' ? 'pH' : 'TDS'}</strong><small>{snapshot?.[probe].state === 'disabled' ? 'Not enabled' : !warning ? 'Status unavailable' : warning === 'normal' ? 'No active warning' : warning === 'signal_fault' ? 'Check signal or connection' : warning === 'out_of_range' ? 'Measurement out of range' : 'Measurement unstable'}</small></div></div> })}</aside></div>
  </section>
}