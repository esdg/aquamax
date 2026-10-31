import { useEffect, useState } from 'preact/hooks'
import type { ProbeState } from './live'

type Temperature = {
  state: ProbeState; valueC: number | null; uncalibratedC: number | null
  stable: boolean; lastSeenSecondsAgo: number | null; offsetC: number
  calibrated: boolean; timestamp: number | null; storageError: boolean
}
type CalibrationData = { temperature: Temperature; ph: { state: ProbeState }; tds: { state: ProbeState } }

const labels: Record<ProbeState, string> = {
  connected: 'Connected', disconnected: 'Disconnected', unstable: 'Unstable',
  out_of_range: 'Out of range', disabled: 'Not enabled',
}

export function Calibration({ timezone }: { timezone: string }) {
  const [data, setData] = useState<CalibrationData | null>(null)
  const [reference, setReference] = useState('')
  const [error, setError] = useState('')
  const [actionError, setActionError] = useState('')
  const [notice, setNotice] = useState('')
  const [busy, setBusy] = useState(false)
  const [revision, setRevision] = useState(0)
  useEffect(() => {
    const controller = new AbortController()
    let active = true
    let timer: ReturnType<typeof setTimeout>
    const load = async () => {
      try {
        const response = await fetch('/api/calibration', { cache: 'no-store', signal: controller.signal })
        if (!response.ok) throw new Error('Calibration unavailable')
        const next: CalibrationData = await response.json()
        if (active) { setData(next); setError('') }
      } catch {
        if (active && !controller.signal.aborted) { setData(null); setError('Calibration is unavailable') }
      } finally { if (active) timer = setTimeout(load, 2000) }
    }
    void load()
    return () => { active = false; controller.abort(); clearTimeout(timer) }
  }, [revision])

  const temperature = data?.temperature
  const ready = temperature?.state === 'connected' && temperature.stable && temperature.valueC !== null && !temperature.storageError
  const referenceValue = Number(reference)
  const canCapture = ready && reference.trim() !== '' && Number.isFinite(referenceValue) && referenceValue >= -55 && referenceValue <= 125 &&
    temperature.uncalibratedC !== null && Math.abs(referenceValue - temperature.uncalibratedC) <= 5
  const save = async (action: 'capture' | 'reset') => {
    if (action === 'reset' && !window.confirm('Reset temperature calibration to its factory offset?')) return
    setBusy(true); setActionError(''); setNotice('')
    try {
      const body = new URLSearchParams(action === 'capture' ? { action, referenceC: reference } : { action, confirm: 'yes' })
      const response = await fetch('/api/calibration/temp', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })
      if (!response.ok) throw new Error(await response.text())
      setReference(''); setNotice(action === 'capture' ? 'Temperature calibration saved.' : 'Temperature calibration reset.')
      setRevision(value => value + 1)
    } catch (reason) { setActionError(reason instanceof Error ? reason.message : 'Calibration could not be saved') }
    finally { setBusy(false) }
  }
  const date = temperature?.timestamp ? new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short', timeZone: timezone }).format(temperature.timestamp * 1000) : 'Date unavailable'

  return <section class="calibration-view">
    <div class="history-heading"><div><div class="eyebrow">AQUARIUM / CALIBRATION</div><h1>Calibration &amp; health</h1></div></div>
    <div class="calibration-layout">
      <div class="calibration-main">
        <div class="calibration-section-title"><div><span class="device-label">01 / TEMPERATURE</span><h2>Reference adjustment</h2></div><span class={`probe-badge ${ready ? 'ready' : ''}`}>{temperature ? labels[temperature.state] : 'Waiting'}</span></div>
        <p class="calibration-instruction">Place a trusted reference thermometer beside the probe and wait until both readings are stable.</p>
        <div class="calibration-readings"><div><span>PROBE READING</span><strong>{temperature?.valueC != null ? `${temperature.valueC.toFixed(2)} °C` : '--'}</strong></div><div><span>STABILITY</span><strong>{!temperature ? '--' : temperature.stable ? 'Stable' : 'Waiting'}</strong></div><div><span>CURRENT OFFSET</span><strong>{temperature ? `${temperature.offsetC > 0 ? '+' : ''}${temperature.offsetC.toFixed(2)} °C` : '--'}</strong></div></div>
        {temperature?.storageError && <p class="calibration-error" role="alert">Calibration storage is unavailable. Reset the calibration or check device storage.</p>}
        {temperature && temperature.state !== 'connected' && <p class="calibration-warning">Check the probe, connector, power and data wiring.</p>}
        <form class="calibration-form" onSubmit={event => { event.preventDefault(); void save('capture') }}>
          <label for="temperature-reference">Reference temperature</label>
          <div class="reference-input"><input id="temperature-reference" type="number" step="0.01" min="-55" max="125" inputMode="decimal" value={reference} onInput={event => setReference(event.currentTarget.value)} placeholder="--" /><span>°C</span></div>
          <small>Maximum adjustment: ±5 °C.</small>
          <div class="calibration-actions"><button class="calibration-primary" type="submit" disabled={!canCapture || busy}>Save calibration</button><button type="button" class="calibration-reset" disabled={!temperature || busy || (!temperature.calibrated && !temperature.storageError)} onClick={() => void save('reset')}>Reset</button></div>
        </form>
        {error && <p class="calibration-error" role="alert">{error}</p>}{actionError && <p class="calibration-error" role="alert">{actionError}</p>}{notice && <p class="calibration-notice" role="status">{notice}</p>}
      </div>
      <aside class="probe-health"><span class="device-label">PROBE HEALTH</span><h2>Channel status</h2>
        <div class="health-row"><div><strong>Temperature</strong><small>{temperature?.lastSeenSecondsAgo != null ? `Last seen ${temperature.lastSeenSecondsAgo} s ago` : 'No valid reading yet'}</small><small>{temperature?.calibrated ? `Calibrated: ${date}` : 'Factory offset'}</small></div><span class={`probe-badge ${temperature?.state === 'connected' ? 'ready' : ''}`}>{temperature ? labels[temperature.state] : 'Waiting'}</span></div>
        <div class="health-row"><div><strong>pH</strong><small>Calibration unavailable</small></div><span class="probe-badge">{data ? labels[data.ph.state] : 'Waiting'}</span></div>
        <div class="health-row"><div><strong>TDS</strong><small>Calibration unavailable</small></div><span class="probe-badge">{data ? labels[data.tds.state] : 'Waiting'}</span></div>
      </aside>
    </div>
  </section>
}