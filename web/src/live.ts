export type ProbeState = 'connected' | 'disconnected' | 'unstable' | 'out_of_range' | 'disabled' | 'signal_fault' | 'waiting'
export type ProbeWarning = 'normal' | 'signal_fault' | 'out_of_range' | 'unstable'
export type AlertStatus = { temperature: 'normal' | 'low' | 'high' | 'probe_fault'; event: 'none' | 'low' | 'high' | 'probe_fault' | 'recovered'; sequence: number; settingsError: boolean; ph?: ProbeWarning; tds?: ProbeWarning }
export type Snapshot = {
  temperature: { state: ProbeState; valueC: number | null }
  ph: { state: ProbeState; value: number | null; calibrated?: boolean; calibrationError?: boolean }
  tds: { state: ProbeState; value: number | null; calibrated?: boolean; defaultTemperature?: boolean }
  wifi: 'connected' | 'disconnected'
  uptimeSeconds: number
  time?: { synchronized: boolean; unixSeconds: number | null; local?: string | null; timezone?: string }
  alerts?: AlertStatus
}

export function phCalibrationStatus(ph: Snapshot['ph'] | undefined): string | undefined {
  if (!ph || ph.state !== 'connected' || ph.value !== null) return undefined
  return ph.calibrationError ? 'Calibration storage error' : ph.calibrated === false ? 'Calibration required' : undefined
}

type Socket = Pick<WebSocket, 'close' | 'onopen' | 'onmessage' | 'onclose' | 'onerror'>
type LiveOptions = {
  socket: () => Socket
  request: (signal: AbortSignal) => Promise<unknown>
  setTimer: (callback: () => void, delay: number) => number
  clearTimer: (timer: number) => void
  onSnapshot: (snapshot: Snapshot) => void
  onOffline: () => void
}

const states = ['connected', 'disconnected', 'unstable', 'out_of_range', 'disabled', 'signal_fault', 'waiting']

export function isSnapshot(value: unknown): value is Snapshot {
  if (!value || typeof value !== 'object') return false
  const data = value as Record<string, unknown>
  const reading = (item: unknown, key: string) => {
    if (!item || typeof item !== 'object') return false
    const channel = item as Record<string, unknown>
    return typeof channel.state === 'string' && states.includes(channel.state) &&
      (channel[key] === null || (typeof channel[key] === 'number' && Number.isFinite(channel[key])))
  }
  const alerts = data.alerts as Record<string, unknown> | undefined
  const ph = data.ph as Record<string, unknown> | undefined
  const tds = data.tds as Record<string, unknown> | undefined
  if (ph && ((ph.calibrated !== undefined && typeof ph.calibrated !== 'boolean') ||
    (ph.calibrationError !== undefined && typeof ph.calibrationError !== 'boolean'))) return false
  if (tds && ((tds.calibrated !== undefined && typeof tds.calibrated !== 'boolean') ||
    (tds.defaultTemperature !== undefined && typeof tds.defaultTemperature !== 'boolean'))) return false
  if (alerts !== undefined && (!alerts || typeof alerts !== 'object' ||
    typeof alerts.temperature !== 'string' || !['normal', 'low', 'high', 'probe_fault'].includes(alerts.temperature) ||
    typeof alerts.event !== 'string' || !['none', 'low', 'high', 'probe_fault', 'recovered'].includes(alerts.event) ||
    typeof alerts.sequence !== 'number' || !Number.isSafeInteger(alerts.sequence) || alerts.sequence < 0 || typeof alerts.settingsError !== 'boolean' ||
    ['ph', 'tds'].some(key => alerts[key] !== undefined && (typeof alerts[key] !== 'string' || !['normal', 'signal_fault', 'out_of_range', 'unstable'].includes(alerts[key] as string))))) return false
  return reading(data.temperature, 'valueC') && reading(data.ph, 'value') && reading(data.tds, 'value') &&
    (data.wifi === 'connected' || data.wifi === 'disconnected') &&
    typeof data.uptimeSeconds === 'number' && Number.isFinite(data.uptimeSeconds)
}

export function startLiveFeed(options: LiveOptions): () => void {
  let stopped = false
  let streaming = false
  let socket: Socket | undefined
  let pollTimer: number | undefined
  let retryTimer: number | undefined
  let watchdog: number | undefined
  let controller: AbortController | undefined

  const clear = (timer: number | undefined) => { if (timer !== undefined) options.clearTimer(timer) }
  const poll = async () => {
    if (stopped || streaming || controller) return
    const request = new AbortController()
    controller = request
    const timeout = options.setTimer(() => request.abort(), 4000)
    try {
      const data = await options.request(request.signal)
      if (!isSnapshot(data)) throw new Error('Invalid device state')
      if (!stopped && !streaming && !request.signal.aborted) options.onSnapshot(data)
    } catch {
      if (!stopped && !streaming) options.onOffline()
    } finally {
      clear(timeout)
      if (controller === request) controller = undefined
      if (!stopped && !streaming) pollTimer = options.setTimer(poll, 2000)
    }
  }
  const connect = () => {
    if (stopped) return
    try {
      const current = options.socket()
      socket = current
      const armWatchdog = () => {
        clear(watchdog)
        watchdog = options.setTimer(() => current.close(), 6000)
      }
      armWatchdog()
      current.onmessage = event => {
        if (stopped || socket !== current) return
        try {
          const data: unknown = JSON.parse(String(event.data))
          if (!isSnapshot(data)) throw new Error('Invalid device state')
          streaming = true
          clear(pollTimer)
          controller?.abort()
          options.onSnapshot(data)
          armWatchdog()
        } catch { current.close() }
      }
      current.onerror = () => current.close()
      current.onclose = () => {
        if (stopped || socket !== current) return
        clear(watchdog)
        streaming = false
        void poll()
        retryTimer = options.setTimer(connect, 3000)
      }
    } catch {
      if (!stopped) retryTimer = options.setTimer(connect, 3000)
    }
  }
  void poll()
  connect()
  return () => {
    stopped = true
    clear(pollTimer)
    clear(retryTimer)
    clear(watchdog)
    controller?.abort()
    socket?.close()
  }
}