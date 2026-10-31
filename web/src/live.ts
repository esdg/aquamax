export type ProbeState = 'connected' | 'disconnected' | 'unstable' | 'out_of_range' | 'disabled'
export type Snapshot = {
  temperature: { state: ProbeState; valueC: number | null }
  ph: { state: ProbeState; value: number | null }
  tds: { state: ProbeState; value: number | null }
  wifi: 'connected' | 'disconnected'
  uptimeSeconds: number
  time?: { synchronized: boolean; unixSeconds: number | null; local?: string | null; timezone?: string }
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

const states = ['connected', 'disconnected', 'unstable', 'out_of_range', 'disabled']

export function isSnapshot(value: unknown): value is Snapshot {
  if (!value || typeof value !== 'object') return false
  const data = value as Record<string, unknown>
  const reading = (item: unknown, key: string) => {
    if (!item || typeof item !== 'object') return false
    const channel = item as Record<string, unknown>
    return typeof channel.state === 'string' && states.includes(channel.state) &&
      (channel[key] === null || (typeof channel[key] === 'number' && Number.isFinite(channel[key])))
  }
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