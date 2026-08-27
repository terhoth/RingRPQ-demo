import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'

const FRONTEND_DIR = fileURLToPath(new URL('.', import.meta.url))
// ports.conf at the repo root is the single place the backend's port is set
// (see ports.sh). Reading it here means `npm run dev` follows a changed port
// without a second copy of the number living in frontend/.env.
const PORTS_CONFIG_FILE = fileURLToPath(new URL('../ports.conf', import.meta.url))

// $RINGRPQ_BACKEND_PORT, then ports.conf, then the default — ports.sh's
// precedence. Last assignment in the file wins, as it does there.
function backendPort() {
  if (process.env.RINGRPQ_BACKEND_PORT) return process.env.RINGRPQ_BACKEND_PORT
  let port
  try {
    for (const line of readFileSync(PORTS_CONFIG_FILE, 'utf8').split('\n')) {
      const [name, ...rest] = line.split('#')[0].split('=')
      if (name.trim() === 'RINGRPQ_BACKEND_PORT' && rest.join('=').trim()) {
        port = rest.join('=').trim()
      }
    }
  } catch {
    // No ports.conf, or unreadable — the default below still gives a working
    // dev server, which is the only thing this path feeds.
  }
  return port ?? '8000'
}

export default defineConfig(({ command, mode }) => {
  // Dev server only. Vite gives process.env precedence over .env files, so
  // doing this for `vite build` would override .env.production's empty
  // VITE_API_BASE_URL — the thing that keeps the production bundle's API
  // calls relative to whichever origin the backend serves it from.
  //
  // That same precedence is why the .env files are consulted first rather
  // than just writing process.env when it happens to be unset: an explicit
  // VITE_API_BASE_URL in frontend/.env(.local) is a deliberate override
  // (a backend on another host), and would otherwise lose to the value
  // derived here.
  if (command === 'serve') {
    const fileEnv = loadEnv(mode, FRONTEND_DIR, 'VITE_')
    if (fileEnv.VITE_API_BASE_URL === undefined) {
      process.env.VITE_API_BASE_URL = `http://localhost:${backendPort()}`
    }
  }
  return {
    plugins: [react()],
  }
})
