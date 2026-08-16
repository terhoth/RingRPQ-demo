import { useEffect, useState } from 'react'
import './QueryForm.css'

import { getDatabaseList } from './services/databases'
import { submitQuery } from './services/query'

const QueryForm = () => {
  const [databaseList, setDatabaseList] = useState([])
  const [databaseName, setDatabaseName] = useState('')
  const [timeoutSeconds, setTimeoutSeconds] = useState('')
  const [maxResults, setMaxResults] = useState('')
  const [query, setQuery] = useState('')
  const [isSubmitting, setIsSubmitting] = useState(false)
  const [error, setError] = useState(null)

  useEffect(() => {
    const fetchDatabases = async () => {
      try {
        const databases = await getDatabaseList()
        setDatabaseList(databases)
      } catch (err) {
        console.error('Error fetching database list:', err)
        setError('Could not load the list of databases.')
      }
    }

    fetchDatabases()
  }, [])

  const handleSubmit = async (event) => {
    event.preventDefault()

    if (!databaseName) {
      setError('Please select a database.')
      return
    }
    if (!query.trim()) {
      setError('Please enter a query.')
      return
    }

    setError(null)
    setIsSubmitting(true)

    try {
      await submitQuery({
        databaseName,
        timeoutSeconds: timeoutSeconds === '' ? undefined : Number(timeoutSeconds),
        maxResults: maxResults === '' ? undefined : Number(maxResults),
        query,
      })
    } catch (err) {
      console.error('Error submitting query:', err)
      setError(err.message || 'An error occurred while submitting the query.')
    } finally {
      setIsSubmitting(false)
    }
  }

  return (
    <form className="queryForm" onSubmit={handleSubmit}>
      <div className="formField">
        <label htmlFor="databaseName">Database</label>
        <select
          id="databaseName"
          value={databaseName}
          onChange={(e) => setDatabaseName(e.target.value)}
          disabled={isSubmitting}
        >
          <option value="">Select a database…</option>
          {databaseList.map((name) => (
            <option key={name} value={name}>
              {name}
            </option>
          ))}
        </select>
      </div>

      <div className="formRow">
        <div className="formField">
          <label htmlFor="timeoutSeconds">Timeout (seconds)</label>
          <input
            id="timeoutSeconds"
            type="number"
            min="0"
            step="1"
            placeholder="No limit"
            value={timeoutSeconds}
            onChange={(e) => setTimeoutSeconds(e.target.value)}
            disabled={isSubmitting}
          />
        </div>

        <div className="formField">
          <label htmlFor="maxResults">Max answers</label>
          <input
            id="maxResults"
            type="number"
            min="0"
            step="1"
            placeholder="No limit"
            value={maxResults}
            onChange={(e) => setMaxResults(e.target.value)}
            disabled={isSubmitting}
          />
        </div>
      </div>

      <div className="formField">
        <label htmlFor="queryString">Query</label>
        <input
          id="queryString"
          type="text"
          placeholder="Enter your query…"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          disabled={isSubmitting}
        />
      </div>

      <button type="submit" className="submitButton" disabled={isSubmitting}>
        {isSubmitting ? 'Submitting…' : 'Submit query'}
      </button>

      {error && <p className="errorMessage">{error}</p>}
    </form>
  )
}

export default QueryForm
