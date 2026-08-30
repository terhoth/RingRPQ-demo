import { Fragment, useEffect, useRef, useState } from 'react'
import './QueryForm.css'

import { getDatabases } from './services/databases'
import { submitQuery, pollQueryResult } from './services/query'
import QueryResult from './QueryResult'

const QueryForm = () => {
  const [databaseList, setDatabaseList] = useState([])
  const [databaseDocs, setDatabaseDocs] = useState({})
  const [isLoadingDatabases, setIsLoadingDatabases] = useState(true)
  const [databaseName, setDatabaseName] = useState('')
  const [timeoutSeconds, setTimeoutSeconds] = useState('')
  const [maxResults, setMaxResults] = useState('')
  const [query, setQuery] = useState('')
  const [isSubmitting, setIsSubmitting] = useState(false)
  const [error, setError] = useState(null)
  const [queryState, setQueryState] = useState(null)
  const cancelPollRef = useRef(null)

  useEffect(() => {
    return () => cancelPollRef.current?.()
  }, [])

  useEffect(() => {
    const fetchDatabases = async () => {
      setIsLoadingDatabases(true)
      try {
        const { graphs, docs } = await getDatabases()
        setDatabaseList(graphs)
        setDatabaseDocs(docs)
      } catch (err) {
        console.error('Error fetching database list:', err)
        setError('Could not load the list of databases.')
      } finally {
        setIsLoadingDatabases(false)
      }
    }

    fetchDatabases()
  }, [])

  // Both fields are optional in graph_doc.json, so each is checked on its own;
  // the type guards keep a malformed entry from taking the whole form down.
  const selectedDoc = databaseDocs[databaseName]
  const docLinks = Array.isArray(selectedDoc?.links)
    ? selectedDoc.links.filter((link) => typeof link === 'string')
    : []
  const docNote = typeof selectedDoc?.note === 'string' ? selectedDoc.note : ''

  let databasePlaceholder = 'Select a database…'
  if (isLoadingDatabases) {
    databasePlaceholder = 'Loading databases…'
  } else if (databaseList.length === 0) {
    databasePlaceholder = 'No databases available'
  }

  const handleSubmit = async (event) => {
    event.preventDefault()

    if (isLoadingDatabases) {
      setError('Still loading the list of databases — please wait.')
      return
    }
    if (!databaseName) {
      setError('Please select a database.')
      return
    }
    if (!query.trim()) {
      setError('Please enter a query.')
      return
    }

    cancelPollRef.current?.()
    setError(null)
    setIsSubmitting(true)
    setQueryState({ status: 'submitting' })

    try {
      const response = await submitQuery({
        databaseName,
        timeoutSeconds: timeoutSeconds === '' ? undefined : Number(timeoutSeconds),
        maxResults: maxResults === '' ? undefined : Number(maxResults),
        query,
      })

      if (response.status === 'translation_failed') {
        setQueryState({
          status: 'translation_failed',
          id: response.translation_failure_id,
          errorMessage: response.error_message,
        })
        return
      }

      const id = response.query_cache_id
      setQueryState({ status: 'pending', id })

      cancelPollRef.current = pollQueryResult(id, {
        onUpdate: (data) => {
          setQueryState({
            status: data.status,
            id: data.id,
            result: data.result,
            errorMessage: data.error_message,
            translationWarningMessage: data.translation_warning_message,
            submittedAt: data.submitted_at,
            completedAt: data.completed_at,
          })
        },
        onError: (err) => {
          console.error('Error polling query result:', err)
          setError('Lost connection while waiting for the result.')
        },
      })
    } catch (err) {
      console.error('Error submitting query:', err)
      setError(err.message || 'An error occurred while submitting the query.')
      setQueryState(null)
    } finally {
      setIsSubmitting(false)
    }
  }

  return (
    <>
      <form className="queryForm" onSubmit={handleSubmit}>
        <div className="formField">
          <label htmlFor="databaseName">Database</label>
          <select
            id="databaseName"
            value={databaseName}
            onChange={(e) => setDatabaseName(e.target.value)}
            disabled={isSubmitting || isLoadingDatabases || databaseList.length === 0}
          >
            <option value="">{databasePlaceholder}</option>
            {databaseList.map((name) => (
              <option key={name} value={name}>
                {name}
              </option>
            ))}
          </select>
          {docLinks.length > 0 && (
            <p className="databaseDocLink">
              Documentation for this graph is available{' '}
              {docLinks.map((link, index) => (
                <Fragment key={link}>
                  {index > 0 &&
                    (index === docLinks.length - 1
                      ? docLinks.length > 2
                        ? ', and '
                        : ' and '
                      : ', ')}
                  <a href={link} target="_blank" rel="noopener noreferrer">
                    here
                  </a>
                </Fragment>
              ))}.
            </p>
          )}
          {docNote && <p className="databaseDocNote">{docNote}</p>}
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

      <QueryResult queryState={queryState} />
    </>
  )
}

export default QueryForm
