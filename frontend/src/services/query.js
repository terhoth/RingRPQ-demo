import axios from 'axios'

// '??' not '||': in production the backend serves this bundle from its own
// origin (see backend/app/main.py's static mount), and .env.production sets
// VITE_API_BASE_URL to the empty string so these calls stay relative. '||'
// would treat that empty string as unset and fall back to the dev URL.
const API_BASE_URL = import.meta.env.VITE_API_BASE_URL ?? 'http://localhost:8000'

// backend/app/routers/query.py's QueryRequest requires both as non-nullable ints
// with no "unlimited" sentinel defined yet, so a blank field in the form has to
// become *some* integer. 0 is used here as "no limit" — revisit this convention
// once the Ring-RPQ subprocess is wired up and its actual CLI semantics for "no
// limit" are known.
const NO_LIMIT = 0

/**
 * Submits a query for execution against the selected database.
 *
 * @param {Object} params
 * @param {string} params.databaseName - Name of the database to query, chosen from the dropdown.
 * @param {number} [params.timeoutSeconds] - Optional timeout limit, in seconds.
 * @param {number} [params.maxResults] - Optional cap on the number of answers returned.
 * @param {string} params.query - The query string typed by the user.
 * @returns {Promise<{status: 'translated', query_cache_id: number} | {status: 'translation_failed', translation_failure_id: number, error_message: string | null}>}
 */
export async function submitQuery({ databaseName, timeoutSeconds, maxResults, query }) {
  try {
    const response = await axios.post(`${API_BASE_URL}/query/submit`, {
      query,
      database_name: databaseName,
      max_number_of_results: maxResults ?? NO_LIMIT,
      time_out_limit: timeoutSeconds ?? NO_LIMIT,
    })
    return response.data
  } catch (error) {
    const message =
      error.response?.data?.detail || 'An unexpected error occurred while submitting the query.'
    console.error('Query submission failed:', message)
    throw new Error(message, { cause: error })
  }
}

export async function getQueryResult(id) {
  const response = await axios.get(`${API_BASE_URL}/query/rpq/${id}`)
  return response.data
}

const POLL_INITIAL_DELAY_MS = 500
const POLL_MAX_DELAY_MS = 5000
const POLL_BACKOFF_FACTOR = 1.5
const TERMINAL_STATUSES = new Set(['completed', 'failed'])

/**
 * Polls GET /query/rpq/{id} until the query reaches a terminal status
 * (completed/failed), calling onUpdate with the raw response on every poll.
 *
 * Uses a recursive setTimeout rather than setInterval, so the next poll is
 * only scheduled once the previous request has resolved — a slow backend
 * can't cause requests to pile up. The delay between polls grows from
 * POLL_INITIAL_DELAY_MS towards POLL_MAX_DELAY_MS on each non-terminal poll,
 * so quick queries get near-immediate updates while long-running ones don't
 * keep hammering the backend every 500ms.
 *
 * @returns {() => void} cancel - call to stop polling early (new submission, unmount).
 */
export function pollQueryResult(id, { onUpdate, onError }) {
  let cancelled = false
  let timeoutId = null
  let delay = POLL_INITIAL_DELAY_MS

  const tick = async () => {
    let data
    try {
      data = await getQueryResult(id)
    } catch (error) {
      if (!cancelled) onError?.(error)
      return
    }

    if (cancelled) return
    onUpdate(data)
    if (TERMINAL_STATUSES.has(data.status)) return

    timeoutId = setTimeout(tick, delay)
    delay = Math.min(delay * POLL_BACKOFF_FACTOR, POLL_MAX_DELAY_MS)
  }

  tick()

  return () => {
    cancelled = true
    clearTimeout(timeoutId)
  }
}
