import { useEffect, useState } from 'preact/hooks'

type Diagnostics = { uptimeSeconds: number; freeHeapBytes: number; freePsramBytes: number; psramBytes: number; flashBytes: number; filesystemReady: boolean; filesystemUsedBytes: number; filesystemTotalBytes: number; journalReady: boolean; wifiConnected: boolean; wifiRssi: number | null; ipAddress: string | null }
type Backup = { version: 1; timezone: string; alerts: { thresholdsEnabled: boolean; probeFaultEnabled: boolean; minimum: number; maximum: number; hysteresis: number; debounceSeconds: number; repeatSeconds: number }; temperatureCalibration: { offsetC: number; timestamp: number; calibrated: boolean } }

function parseBackup(value: unknown): Backup {
  if (!value || typeof value !== 'object') throw new Error('Invalid configuration file')
  const data = value as Backup
  const alerts = data.alerts
  const calibration = data.temperatureCalibration
  if (data.version !== 1 || typeof data.timezone !== 'string' || !alerts || !calibration ||
      typeof alerts.thresholdsEnabled !== 'boolean' || typeof alerts.probeFaultEnabled !== 'boolean' ||
      ![alerts.minimum, alerts.maximum, alerts.hysteresis, alerts.debounceSeconds, alerts.repeatSeconds, calibration.offsetC, calibration.timestamp].every(value => typeof value === 'number' && Number.isFinite(value)) ||
      typeof calibration.calibrated !== 'boolean') throw new Error('Invalid configuration file')
  return data
}

export function Settings({ timezone, pending, error, online, timezones, onTimezone }: { timezone: string; pending: string | null; error: boolean; online: boolean; timezones: string[]; onTimezone: (value: string) => void }) {
  return <section><div class="history-heading"><div><div class="eyebrow">AQUARIUM / SETTINGS</div><h1>Settings</h1></div></div>
    <div class="system-section"><h2>Device time</h2><label class="system-field">Timezone<select value={pending ?? timezone} disabled={!online || pending !== null} onChange={event => onTimezone(event.currentTarget.value)}>{timezones.map(zone => <option value={zone}>{zone.replaceAll('_', ' ')}</option>)}</select></label>{error && <p class="calibration-error" role="alert">Could not save timezone.</p>}</div>
    <div class="system-section"><h2>Network</h2><p>{online ? 'Wi-Fi connected' : 'Device offline'}</p><p>Wi-Fi credentials are not included in configuration backups.</p></div>
    <div class="system-section"><h2>Measurements and storage</h2><p>Temperature is enabled. pH, TDS and external history export are not enabled.</p><p>History is stored locally with fixed retention.</p></div>
  </section>
}

export function System() {
  const [diagnostics, setDiagnostics] = useState<Diagnostics | null>(null)
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')
  const [busy, setBusy] = useState(false)
  const [backup, setBackup] = useState<File | null>(null)
  const [firmware, setFirmware] = useState<File | null>(null)
  useEffect(() => {
    const controller = new AbortController()
    let active = true
    let timer: ReturnType<typeof setTimeout>
    const load = async () => {
      try {
        const response = await fetch('/api/system', { cache: 'no-store', signal: controller.signal })
        if (!response.ok) throw new Error('Diagnostics unavailable')
        const data = await response.json() as Diagnostics
        if (active) { setDiagnostics(data); setError('') }
      } catch { if (active && !controller.signal.aborted) { setDiagnostics(null); setError('Diagnostics unavailable') } }
      finally { if (active) timer = setTimeout(load, 10000) }
    }
    void load()
    return () => { active = false; controller.abort(); clearTimeout(timer) }
  }, [])
  const perform = async (action: () => Promise<string>) => {
    setBusy(true); setError(''); setNotice('')
    try { setNotice(await action()) }
    catch (reason) { setError(reason instanceof Error ? reason.message : 'Operation failed') }
    finally { setBusy(false) }
  }
  const restore = async () => {
    if (!backup) return
    await perform(async () => {
      if (backup.size > 4096) throw new Error('Configuration file is too large')
      let data: Backup
      try { data = parseBackup(JSON.parse(await backup.text())) } catch { throw new Error('Invalid configuration file') }
      if (!window.confirm('Restore timezone, temperature calibration and alert settings? This may enable saved alerts.')) return 'Restore cancelled.'
      const { alerts, temperatureCalibration } = data
      const body = new URLSearchParams({ version: '1', confirm: 'yes', timezone: data.timezone, thresholdsEnabled: alerts.thresholdsEnabled ? '1' : '0', probeFaultEnabled: alerts.probeFaultEnabled ? '1' : '0', minimum: String(alerts.minimum), maximum: String(alerts.maximum), hysteresis: String(alerts.hysteresis), debounceSeconds: String(alerts.debounceSeconds), repeatSeconds: String(alerts.repeatSeconds), offsetC: String(temperatureCalibration.offsetC), timestamp: String(temperatureCalibration.timestamp), calibrated: temperatureCalibration.calibrated ? '1' : '0' })
      const response = await fetch('/api/system/config', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })
      if (!response.ok) throw new Error(await response.text())
      return 'Configuration restored. Refresh the dashboard to see the updated timezone.'
    })
  }
  const restart = async () => {
    if (!window.confirm('Restart AquaMax?')) return
    await perform(async () => {
      const response = await fetch('/api/system/reboot', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ confirm: 'yes' }) })
      if (!response.ok) throw new Error(await response.text())
      return 'Restarting. The dashboard will reconnect when the device is ready.'
    })
  }
  const update = async () => {
    if (!firmware) return
    await perform(async () => {
      if (!firmware.name.endsWith('.bin') || firmware.size === 0 || firmware.size > 3 * 1024 * 1024) throw new Error('Select an AquaMax firmware .bin file under 3 MB')
      if (!window.confirm('Install this firmware and restart? Only use a trusted AquaMax build for this device.')) return 'Update cancelled.'
      const body = new FormData(); body.append('file', firmware)
      const response = await fetch('/api/system/ota?confirm=yes', { method: 'POST', body })
      if (!response.ok) throw new Error(await response.text())
      return 'Firmware uploaded. The device is restarting.'
    })
  }
  const megabytes = (bytes: number) => `${(bytes / 1048576).toFixed(2)} MB`
  return <section><div class="history-heading"><div><div class="eyebrow">AQUARIUM / SYSTEM</div><h1>System</h1></div></div>
    <div class="system-section"><h2>Diagnostics</h2>{!diagnostics ? <p>{error || 'Loading diagnostics...'}</p> : <dl class="system-diagnostics"><div><dt>Uptime</dt><dd>{Math.floor(diagnostics.uptimeSeconds / 60)} min</dd></div><div><dt>Network</dt><dd>{diagnostics.wifiConnected ? `${diagnostics.ipAddress} (${diagnostics.wifiRssi} dBm)` : 'Disconnected'}</dd></div><div><dt>Free heap</dt><dd>{megabytes(diagnostics.freeHeapBytes)}</dd></div><div><dt>Free PSRAM</dt><dd>{megabytes(diagnostics.freePsramBytes)} / {megabytes(diagnostics.psramBytes)}</dd></div><div><dt>Flash</dt><dd>{megabytes(diagnostics.flashBytes)}</dd></div><div><dt>Storage</dt><dd>{diagnostics.filesystemReady ? `${megabytes(diagnostics.filesystemUsedBytes)} / ${megabytes(diagnostics.filesystemTotalBytes)}` : 'Unavailable'}</dd></div><div><dt>Journal</dt><dd>{diagnostics.journalReady ? 'Ready' : 'Unavailable'}</dd></div></dl>}</div>
    <div class="system-section"><h2>Configuration</h2><p>Includes timezone, temperature calibration and alert settings. Wi-Fi credentials, history and journal are excluded.</p><div class="system-actions"><a class="notification-button" href="/api/system/config" download="aquamax-config.json">Download backup</a><label class="system-file">Restore backup<input type="file" accept=".json,application/json" onChange={event => setBackup(event.currentTarget.files?.[0] ?? null)} /></label><button class="notification-button" type="button" disabled={!backup || busy} onClick={() => void restore()}>Restore</button></div><small>Restore writes settings separately; a storage failure can leave a partial restore.</small></div>
    <div class="system-section"><h2>Firmware update</h2><p>Updates are not signed or authenticated. Use only a trusted firmware file on a trusted network.</p><div class="system-actions"><label class="system-file">Firmware file<input type="file" accept=".bin,application/octet-stream" onChange={event => setFirmware(event.currentTarget.files?.[0] ?? null)} /></label><button class="notification-button" type="button" disabled={!firmware || busy} onClick={() => void update()}>Install firmware</button></div></div>
    <div class="system-section"><h2>Restart</h2><button class="notification-button" type="button" disabled={busy} onClick={() => void restart()}>Restart device</button></div>
    {error && <p class="calibration-error" role="alert">{error}</p>}{notice && <p class="calibration-notice" role="status">{notice}</p>}
  </section>
}