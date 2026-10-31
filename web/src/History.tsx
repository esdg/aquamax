import { useEffect, useRef, useState } from 'preact/hooks'
import uPlot from 'uplot'
import 'uplot/dist/uPlot.min.css'
import type { JournalEntry } from './Journal'

type Metric = 'temperature' | 'ph' | 'tds'
type Range = '1h' | '24h' | '7d' | '30d' | '1y'
type Point = [number, number | null, number | null, number | null, number]
type Data = { synchronized: boolean; storageError: boolean; points: Point[] }
type Storage = { state: 'ready' | 'waiting_time' | 'storage_error'; usedBytes: number; totalBytes: number; retention: string[] }

const metrics: { value: Metric; label: string; unit: string; color: string }[] = [
  { value: 'temperature', label: 'Temperature', unit: '°C', color: '#237f7a' },
  { value: 'ph', label: 'pH', unit: 'pH', color: '#786ba6' },
  { value: 'tds', label: 'TDS', unit: 'ppm', color: '#af7a24' },
]
const ranges: Range[] = ['1h', '24h', '7d', '30d', '1y']

function Chart({ points, metric, timezone, events }: { points: Point[]; metric: typeof metrics[number]; timezone: string; events: JournalEntry[] }) {
  const container = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!container.current) return
    const target = container.current
    const dateFormat = new Intl.DateTimeFormat(undefined, { timeZone: timezone, month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' })
    const showPoints = points.filter(point => point[4] > 0).length < 2
    const plot = new uPlot({
      width: Math.max(220, target.clientWidth), height: 310,
      scales: { x: { time: true, range: () => [points[0][0], points[points.length - 1][0] + (points[1]?.[0] - points[0][0] || 1)] } },
      axes: [
        { stroke: '#62766b', grid: { stroke: '#e5ece6', width: 1 }, values: (_plot, ticks) => ticks.map(tick => dateFormat.format(tick * 1000)), space: 140, size: 60 },
        { stroke: '#62766b', grid: { stroke: '#e5ece6', width: 1 }, size: 55 },
      ],
      series: [
        { label: 'Time', value: (_plot, value) => value === null ? '--' : dateFormat.format(value * 1000) },
        { label: 'Minimum', stroke: '#a7b5ac', width: 1, dash: [4, 4], points: { show: showPoints } },
        { label: 'Average', stroke: metric.color, width: 2, points: { show: showPoints } },
        { label: 'Maximum', stroke: '#bf9c61', width: 1, dash: [4, 4], points: { show: showPoints } },
      ],
      hooks: { draw: [chart => {
        const { left, top, width, height } = chart.bbox
        chart.ctx.save()
        chart.ctx.strokeStyle = '#b27753'
        chart.ctx.setLineDash([4, 4])
        for (const event of events) {
          if (event.timestamp === null) continue
          const position = chart.valToPos(event.timestamp, 'x', true)
          if (position < left || position > left + width) continue
          chart.ctx.beginPath(); chart.ctx.moveTo(position, top); chart.ctx.lineTo(position, top + height); chart.ctx.stroke()
        }
        chart.ctx.restore()
      }] },
    }, [points.map(point => point[0]), points.map(point => point[1]), points.map(point => point[2]), points.map(point => point[3])], target)
    const observer = new ResizeObserver(() => plot.setSize({ width: Math.max(220, target.clientWidth), height: 310 }))
    observer.observe(target)
    return () => { observer.disconnect(); plot.destroy() }
  }, [points, metric, timezone, events])
  return <div class="history-plot" ref={container} role="img" aria-label={`${metric.label} history chart, minimum, average and maximum`} />
}

export function History({ timezone }: { timezone: string }) {
  const [metric, setMetric] = useState<Metric>('temperature')
  const [range, setRange] = useState<Range>('1h')
  const [data, setData] = useState<Data | null>(null)
  const [storage, setStorage] = useState<Storage | null>(null)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(true)
  const [clearing, setClearing] = useState(false)
  const [revision, setRevision] = useState(0)
  const [events, setEvents] = useState<JournalEntry[]>([])
  const [eventsError, setEventsError] = useState(false)
  const selected = metrics.find(item => item.value === metric)!

  useEffect(() => {
    const controller = new AbortController()
    let active = true
    let timer: ReturnType<typeof setTimeout>
    const load = async () => {
      try {
        const response = await fetch('/api/events', { cache: 'no-store', signal: controller.signal })
        if (!response.ok) throw new Error('Events unavailable')
        const data: { events: JournalEntry[] } = await response.json()
        if (active) { setEvents(data.events); setEventsError(false) }
      } catch { if (active && !controller.signal.aborted) { setEvents([]); setEventsError(true) } }
      finally { if (active) timer = setTimeout(load, 30000) }
    }
    void load()
    return () => { active = false; controller.abort(); clearTimeout(timer) }
  }, [])

  useEffect(() => {
    const controller = new AbortController()
    let timer: ReturnType<typeof setTimeout>
    let active = true
    const load = async () => {
      try {
        const [response, statusResponse] = await Promise.all([
          fetch(`/api/history?metric=${metric}&range=${range}`, { cache: 'no-store', signal: controller.signal }),
          fetch('/api/history/status', { cache: 'no-store', signal: controller.signal }),
        ])
        if (!response.ok || !statusResponse.ok) throw new Error('History is unavailable')
        const next: Data = await response.json()
        const status: Storage = await statusResponse.json()
        if (!Array.isArray(next.points) || next.storageError || status.state === 'storage_error') throw new Error('History storage is unavailable')
        if (active) { setData(next); setStorage(status); setError(''); setLoading(false) }
      } catch {
        if (active && !controller.signal.aborted) { setData(null); setError('History is unavailable'); setLoading(false) }
      } finally {
        if (active) timer = setTimeout(load, 30000)
      }
    }
    setLoading(true)
    setData(null)
    void load()
    return () => { active = false; controller.abort(); clearTimeout(timer) }
  }, [metric, range, revision])

  const clear = async () => {
    if (!window.confirm('Delete all temperature, pH and TDS history? This cannot be undone.')) return
    setClearing(true)
    try {
      const response = await fetch('/api/history', { method: 'DELETE', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'confirm=yes' })
      if (!response.ok) throw new Error('Clear failed')
      setRevision(value => value + 1)
    } catch { setError('Could not clear history') }
    finally { setClearing(false) }
  }

  const recorded = data?.points.filter(point => point[4] > 0) ?? []
  const minimum = recorded.length ? Math.min(...recorded.map(point => point[1]!)) : null
  const maximum = recorded.length ? Math.max(...recorded.map(point => point[3]!)) : null
  const samples = recorded.reduce((total, point) => total + point[4], 0)
  const average = samples ? recorded.reduce((total, point) => total + point[2]! * point[4], 0) / samples : null
  const format = (value: number | null) => value === null ? '--' : `${value.toFixed(metric === 'tds' ? 0 : 2)} ${selected.unit}`
  const firstTime = data?.points[0]?.[0] ?? 0
  const lastTime = data?.points.length ? data.points[data.points.length - 1][0] + (data.points[1]?.[0] - firstTime || 1) : 0
  const visibleEvents = events.filter(event => event.timestamp !== null && event.timestamp >= firstTime && event.timestamp < lastTime)
  const formatEventTime = (timestamp: number) => new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short', timeZone: timezone }).format(timestamp * 1000)

  return <section class="history-view">
    <div class="history-heading"><div><div class="eyebrow">AQUARIUM / HISTORY</div><h1>Measurement history</h1></div><a class="history-export" href={`/api/history/export.csv?metric=${metric}&range=${range}`} download>Export CSV</a></div>
    <div class="history-controls">
      <div class="segment" role="group" aria-label="Measurement">{metrics.map(item => <button type="button" class={metric === item.value ? 'selected' : ''} aria-pressed={metric === item.value} onClick={() => setMetric(item.value)}>{item.label}</button>)}</div>
      <div class="segment" role="group" aria-label="Time range">{ranges.map(item => <button type="button" class={range === item ? 'selected' : ''} aria-pressed={range === item} onClick={() => setRange(item)}>{item}</button>)}</div>
    </div>
    <div class="history-summary"><div><span>MINIMUM</span><strong>{format(minimum)}</strong></div><div><span>AVERAGE</span><strong>{format(average)}</strong></div><div><span>MAXIMUM</span><strong>{format(maximum)}</strong></div></div>
    <div class="history-chart-area">
      {loading ? <p class="history-placeholder" role="status">Loading history...</p> : error ? <p class="history-placeholder" role="alert">{error}</p> : !data?.synchronized ? <p class="history-placeholder">Waiting for device time synchronization.</p> : !recorded.length ? <p class="history-placeholder">No {selected.label.toLowerCase()} readings in this range.</p> : <Chart points={data.points} metric={selected} timezone={timezone} events={visibleEvents} />}
    </div>
    {eventsError && <p class="calibration-warning">Journal events are unavailable.</p>}
    {visibleEvents.length > 0 && <div class="history-events"><strong>Events in this range</strong>{visibleEvents.map(event => <div key={event.id}><time>{formatEventTime(event.timestamp!)}</time><span>{event.text}</span></div>)}</div>}
    <div class="history-footer"><div><strong>Storage</strong><span>{storage ? `${(storage.usedBytes / 1048576).toFixed(2)} / ${(storage.totalBytes / 1048576).toFixed(2)} MB` : '--'}</span><small>{storage?.retention.join(' · ')}</small></div><button type="button" class="history-clear" disabled={clearing} onClick={() => void clear()}>Clear history</button></div>
  </section>
}