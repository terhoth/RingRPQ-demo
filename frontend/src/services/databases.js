import axios from 'axios'

// Empty VITE_API_BASE_URL means same-origin — but the bundle may be served
// under a path prefix (vite's `base`), so resolve against that rather than
// the origin root. BASE_URL always carries a trailing slash; the paths below
// supply their own.
const API_BASE_URL =
  import.meta.env.VITE_API_BASE_URL ||
  (import.meta.env.PROD ? import.meta.env.BASE_URL.replace(/\/$/, '') : 'http://localhost:8000')

/**
 * @returns {Promise<{graphs: string[], docs: Record<string, {links: string[], note: string}>}>}
 *   graphs - available database names, for the dropdown.
 *   docs - maps a subset of those names to that graph's documentation: any
 *   number of URLs plus a literal note shown below them. Either part can be
 *   empty, and a name with no entry here shows nothing at all.
 */
export async function getDatabases() {
  try {
    const response = await axios.get(`${API_BASE_URL}/query/databases`)
    return {
      graphs: response.data.graphs,
      docs: response.data.docs || {},
    }
  } catch (error) {
    console.error('Error getting database list:', error)
    throw error
  }
}
