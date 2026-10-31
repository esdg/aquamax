import { render } from 'preact'
import './index.css'
import { App } from './app.tsx'

render(<App />, document.getElementById('app')!)

if (window.isSecureContext && 'serviceWorker' in navigator) {
	void navigator.serviceWorker.register('/service-worker.js').catch(() => {})
}
