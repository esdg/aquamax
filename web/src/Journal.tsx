import { useEffect, useState } from 'preact/hooks'

export type JournalEntry = { id: number; timestamp: number | null; kind: 'note' | 'alert' | 'recovery'; text: string }

export function Journal({ timezone }: { timezone: string }) {
  const [events, setEvents] = useState<JournalEntry[] | null>(null)
  const [tips, setTips] = useState<string[]>([])
  const [text, setText] = useState('')
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const [revision, setRevision] = useState(0)
  useEffect(() => {
    const controller = new AbortController()
    void Promise.all([fetch('/api/events', { cache: 'no-store', signal: controller.signal }), fetch('/api/tips', { cache: 'no-store', signal: controller.signal })]).then(async ([eventsResponse, tipsResponse]) => {
      if (!eventsResponse.ok || !tipsResponse.ok) throw new Error('Journal unavailable')
      const eventData: { events: JournalEntry[] } = await eventsResponse.json()
      const tipData: { tips: string[] } = await tipsResponse.json()
      setEvents(eventData.events); setTips(tipData.tips); setError('')
    }).catch(() => { if (!controller.signal.aborted) setError('Journal unavailable') })
    return () => controller.abort()
  }, [revision])
  const bytes = new TextEncoder().encode(text).length
  const add = async () => {
    setBusy(true); setError('')
    try {
      const response = await fetch('/api/events', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ text: text.trim() }) })
      if (!response.ok) throw new Error(await response.text())
      setText(''); setRevision(value => value + 1)
    } catch (reason) { setError(reason instanceof Error ? reason.message : 'Could not save note') }
    finally { setBusy(false) }
  }
  const remove = async (id: number) => {
    if (!window.confirm('Delete this journal entry?')) return
    setBusy(true); setError('')
    try {
      const response = await fetch('/api/events', { method: 'DELETE', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams({ id: String(id), confirm: 'yes' }) })
      if (!response.ok) throw new Error(await response.text())
      setRevision(value => value + 1)
    } catch (reason) { setError(reason instanceof Error ? reason.message : 'Could not delete entry') }
    finally { setBusy(false) }
  }
  const formatDate = (timestamp: number | null) => timestamp ? new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short', timeZone: timezone }).format(timestamp * 1000) : 'Time unavailable'
  return <section><div class="history-heading"><div><div class="eyebrow">AQUARIUM / JOURNAL</div><h1>Journal &amp; tips</h1></div></div>
    <div class="journal-layout"><div><form class="journal-form" onSubmit={event => { event.preventDefault(); void add() }}><label for="journal-note">New note</label><textarea id="journal-note" rows={3} value={text} onInput={event => setText(event.currentTarget.value)} placeholder="Water change, observation..." /><div class="journal-form-footer"><small>{bytes} / 120 bytes</small><button class="calibration-primary" type="submit" disabled={busy || !text.trim() || bytes > 120 || /[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/.test(text)}>Add note</button></div></form>
      {error && <p class="calibration-error" role="alert">{error}</p>}
      <div class="journal-list"><div class="section-label"><h2>Recent activity</h2><span>{events ? `${events.length} / 100` : '--'}</span></div>{events === null ? <p class="journal-empty">Loading journal...</p> : !events.length ? <p class="journal-empty">No entries yet.</p> : events.map(entry => <article class="journal-entry" key={entry.id}><div><span class="journal-kind">{entry.kind === 'note' ? 'NOTE' : entry.kind === 'alert' ? 'ALERT' : 'RECOVERY'}</span><p>{entry.text}</p><small>{formatDate(entry.timestamp)}</small></div><button type="button" disabled={busy} onClick={() => void remove(entry.id)} aria-label={`Delete ${entry.kind}: ${entry.text}`}>Delete</button></article>)}</div>
    </div><aside class="probe-health"><span class="device-label">CONTEXT</span><h2>Tips</h2>{tips.length ? tips.map(tip => <p class="calibration-instruction">{tip}</p>) : <p class="calibration-instruction">No tips right now.</p>}</aside></div>
  </section>
}