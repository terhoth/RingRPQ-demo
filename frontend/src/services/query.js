/**
 * Submits a query for execution against the selected database.
 *
 * @param {Object} params
 * @param {string} params.databaseName - Name of the database to query, chosen from the dropdown.
 * @param {number} [params.timeoutSeconds] - Optional timeout limit, in seconds.
 * @param {number} [params.maxResults] - Optional cap on the number of answers returned.
 * @param {string} params.query - The query string typed by the user.
 *
 * Placeholder only — wire this up to `POST {VITE_API_BASE_URL}/query/submit`
 * (payload shape: `{ query, database_name, max_number_of_results, time_out_limit }`)
 * once that endpoint is implemented in backend/app/routers/query.py.
 */
export async function submitQuery({ databaseName, timeoutSeconds, maxResults, query }) {
  console.log('submitQuery placeholder called with:', {
    databaseName,
    timeoutSeconds,
    maxResults,
    query,
  })

  throw new Error('Query submission is not implemented yet.')
}
