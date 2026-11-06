import { useEffect, useState } from 'preact/hooks'
import type { ProbeState } from './live'

type Temperature = {
  state: ProbeState; valueC: number | null; uncalibratedC: number | null
  stable: boolean; lastSeenSecondsAgo: number | null; offsetC: number
  calibrated: boolean; timestamp: number | null; storageError: boolean
}
type PhCalibration = {
  state: ProbeState; enabled?: boolean; adcVolts?: number | null; boardVolts?: number | null
  stable?: boolean; calibrated?: boolean; storageError?: boolean; active?: boolean
  points?: { ph: number; boardVolts: number }[]
}
type TdsCalibration = { state: ProbeState; enabled?: boolean; adcVolts?: number | null; stable?: boolean; calibrated?: boolean; storageError?: boolean; active?: boolean; temperatureC?: number; defaultTemperature?: boolean }
type CalibrationData = { temperature: Temperature; ph: PhCalibration; tds: TdsCalibration }

const labels: Record<ProbeState, string> = {
  connected: 'Connected', disconnected: 'Disconnected', unstable: 'Unstable',
  out_of_range: 'Out of range', disabled: 'Not enabled', signal_fault: 'Check signal or connection', waiting: 'Waiting for measurement',
}

export function Calibration({ timezone }: { timezone: string }) {
  const [data, setData] = useState<CalibrationData | null>(null)
  const [reference, setReference] = useState('')
  const [phReference, setPhReference] = useState('6.86')
  const [tdsReference, setTdsReference] = useState('707')
  const [tdsMessage, setTdsMessage] = useState('')
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
  const ph = data?.ph
  const phPoints = ph?.points ?? []
  const phReferenceValue = Number(phReference)
  const canCapturePh = ph?.active && ph.state === 'connected' && ph.stable && ph.boardVolts != null &&
    phReference.trim() !== '' && Number.isFinite(phReferenceValue) && phReferenceValue >= 0 && phReferenceValue <= 14
  const changePh = async (action: 'start' | 'capture' | 'save' | 'cancel') => {
    setBusy(true); setActionError(''); setNotice('')
    try {
      const body = new URLSearchParams(action === 'capture' ? { action, referencePh: phReference } : { action })
      const response = await fetch('/api/calibration/ph', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })
      if (!response.ok) throw new Error(await response.text())
      if (action === 'save') setNotice('pH calibration saved.')
      if (action === 'cancel') setNotice('pH calibration cancelled.')
      setRevision(value => value + 1)
    } catch (reason) { setActionError(reason instanceof Error ? reason.message : 'pH calibration could not be updated') }
    finally { setBusy(false) }
  }
  const tds = data?.tds
  const tdsReferenceValue = Number(tdsReference)
  const canSaveTds = tds?.active && tds.stable && tds.adcVolts != null && tdsReference.trim() !== '' &&
    Number.isFinite(tdsReferenceValue) && tdsReferenceValue > 0 && tdsReferenceValue <= 1000
  const changeTds = async (action: 'start' | 'save' | 'cancel') => {
    setBusy(true); setTdsMessage('')
    try {
      const body = new URLSearchParams(action === 'save' ? { action, referencePpm: tdsReference } : { action })
      const response = await fetch('/api/calibration/tds', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })
      if (!response.ok) throw new Error(await response.text())
      if (action === 'save') setTdsMessage('TDS calibration saved.')
      if (action === 'cancel') setTdsMessage('TDS calibration cancelled.')
      setRevision(value => value + 1)
    } catch (reason) { setTdsMessage(reason instanceof Error ? reason.message : 'TDS calibration could not be updated') }
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
        {ph?.enabled && <div class="ph-calibration">
          <div class="calibration-section-title"><div><span class="device-label">02 / pH</span><h2>Buffer calibration</h2></div><span class={`probe-badge ${ph.state === 'connected' ? 'ready' : ''}`}>{ph.state === 'connected' ? 'Signal available' : labels[ph.state]}</span></div>
          <p class="calibration-instruction">Rinse the probe between buffers and allow it to settle for at least 60 seconds in each solution.</p>
          <div class="calibration-readings"><div><span>BOARD VOLTAGE</span><strong>{ph.boardVolts != null ? `${ph.boardVolts.toFixed(3)} V` : '--'}</strong></div><div><span>STABILITY</span><strong>{ph.stable ? 'Stable' : 'Waiting'}</strong></div><div><span>CALIBRATION</span><strong>{ph.calibrated ? 'Saved' : 'Required'}</strong></div></div>
          {ph.storageError && <p class="calibration-error" role="alert">pH calibration storage is unavailable.</p>}
          {!ph.active ? <div class="calibration-actions"><button class="calibration-primary" type="button" disabled={busy || ph.storageError} onClick={() => void changePh('start')}>Start calibration</button></div> : <form class="calibration-form" onSubmit={event => { event.preventDefault(); void changePh('capture') }}>
            <label for="ph-reference">Buffer pH</label>
            <div class="reference-input"><input id="ph-reference" type="number" step="0.01" min="0" max="14" inputMode="decimal" value={phReference} onInput={event => setPhReference(event.currentTarget.value)} /><span>pH</span></div>
            <small>{phPoints.length ? `Captured: ${phPoints.map(point => point.ph.toFixed(2)).join(', ')}` : 'No buffers captured'}</small>
            <div class="calibration-actions"><button class="calibration-primary" type="submit" disabled={!canCapturePh || busy}>Capture buffer</button><button type="button" disabled={phPoints.length < 2 || busy} onClick={() => void changePh('save')}>Save calibration</button><button type="button" class="calibration-reset" disabled={busy} onClick={() => void changePh('cancel')}>Cancel</button></div>
          </form>}
        </div>}
        {tds?.enabled && <div class="ph-calibration">
          <div class="calibration-section-title"><div><span class="device-label">03 / TDS</span><h2>Reference solution</h2></div><span class={`probe-badge ${tds.state === 'connected' ? 'ready' : ''}`}>{tds.state === 'connected' ? 'Signal available' : labels[tds.state]}</span></div>
          <p class="calibration-instruction">Rinse the probe, place it in a known TDS standard and allow it to settle before starting. Enter the standard's ppm value at 25 °C on the 0.5 TDS scale.</p>
          <div class="calibration-readings"><div><span>SIGNAL</span><strong>{tds.adcVolts != null ? `${tds.adcVolts.toFixed(3)} V` : '--'}</strong></div><div><span>STABILITY</span><strong>{tds.stable ? 'Stable' : 'Waiting'}</strong></div><div><span>CALIBRATION</span><strong>{tds.calibrated ? 'Saved' : 'Estimate'}</strong></div></div>
          <p class="calibration-instruction">Temperature: {tds.temperatureC != null ? `${tds.temperatureC.toFixed(1)} °C` : '--'}{tds.defaultTemperature ? ' (25 °C assumed; use a 25 °C solution for calibration)' : ' (measured)'}.</p>
          {tds.storageError && <p class="calibration-error" role="alert">TDS calibration storage is unavailable.</p>}
          {!tds.active ? <div class="calibration-actions"><button class="calibration-primary" type="button" disabled={busy || tds.storageError} onClick={() => void changeTds('start')}>Start calibration</button></div> : <form class="calibration-form" onSubmit={event => { event.preventDefault(); void changeTds('save') }}>
            <label for="tds-reference">Reference TDS</label>
            <div class="reference-input"><input id="tds-reference" type="number" step="0.1" min="1" max="1000" inputMode="decimal" value={tdsReference} onInput={event => setTdsReference(event.currentTarget.value)} /><span>ppm</span></div>
            <small>A fresh measurement can take up to 60 seconds.</small>
            <div class="calibration-actions"><button class="calibration-primary" type="submit" disabled={!canSaveTds || busy}>Save calibration</button><button type="button" class="calibration-reset" disabled={busy} onClick={() => void changeTds('cancel')}>Cancel</button></div>
          </form>}
          {tdsMessage && <p class="calibration-notice" role="status">{tdsMessage}</p>}
        </div>}
      </div>
      <aside class="probe-health"><span class="device-label">PROBE HEALTH</span><h2>Channel status</h2>
        <div class="health-row"><div><strong>Temperature</strong><small>{temperature?.lastSeenSecondsAgo != null ? `Last seen ${temperature.lastSeenSecondsAgo} s ago` : 'No valid reading yet'}</small><small>{temperature?.calibrated ? `Calibrated: ${date}` : 'Factory offset'}</small></div><span class={`probe-badge ${temperature?.state === 'connected' ? 'ready' : ''}`}>{temperature ? labels[temperature.state] : 'Waiting'}</span></div>
        <div class="health-row"><div><strong>pH</strong><small>{!ph?.enabled ? 'Not enabled' : ph.calibrated ? 'Calibration saved' : 'Calibration required'}</small></div><span class="probe-badge">{!ph ? 'Waiting' : ph.state === 'connected' ? 'Signal available' : labels[ph.state]}</span></div>
        <div class="health-row"><div><strong>TDS</strong><small>{!tds?.enabled ? 'Not enabled' : tds.calibrated ? 'Calibration saved' : 'Uncalibrated estimate'}</small></div><span class="probe-badge">{!tds ? 'Waiting' : tds.state === 'connected' ? 'Signal available' : labels[tds.state]}</span></div>
      </aside>
    </div>
  </section>
}