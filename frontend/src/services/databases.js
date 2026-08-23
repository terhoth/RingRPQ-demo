import axios from 'axios'

const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || 'http://localhost:8000'

/**
 * @returns {Promise<{graphs: string[], docs: Record<string, string>}>}
 *   graphs - available database names, for the dropdown.
 *   docs - maps a subset of those names to a documentation URL. A name with
 *   no entry here simply has no documentation link to show.
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
