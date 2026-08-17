import { useEffect, useState } from 'react'
import './QueryResult.css'

const STATUS_LABELS = {
  submitting: 'Submitting…',
  pending: 'Waiting to start…',
  running: 'Running…',
  completed: 'Completed',
  failed: 'Failed',
  translation_failed: 'Translation failed',
}

const formatElapsed = (seconds) => {
  if (seconds < 60) return `${seconds}s`
  const minutes = Math.floor(seconds / 60)
  return `${minutes}m ${seconds % 60}s`
}

const QueryResultTable = ({ result }) => {
  if (!result || !Array.isArray(result.columns) || !Array.isArray(result.rows)) {
    return <pre className="queryResultRaw">{JSON.stringify(result, null, 2)}</pre>
  }

  if (result.rows.length === 0) {
    return <p className="queryResultHint">No answers found for this query.</p>
  }

  return (
    <div className="queryResultTableWrapper">
      <p className="queryResultCount">
        {result.rows.length} answer{result.rows.length === 1 ? '' : 's'}
      </p>
      <table className="queryResultTable">
        <thead>
          <tr>
            {result.columns.map((column) => (
              <th key={column}>{column}</th>
            ))}
          </tr>
        </thead>
        <tbody>
          {result.rows.map((row, rowIndex) => (
            <tr key={rowIndex}>
              {row.map((cell, cellIndex) => (
                <td key={cellIndex}>{String(cell)}</td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

const QueryResult = ({ queryState }) => {
  const [elapsedSeconds, setElapsedSeconds] = useState(0)
  const status = queryState?.status
  const isInFlight = status === 'pending' || status === 'running'

  useEffect(() => {
    if (!isInFlight || !queryState.submittedAt) {
      return undefined
    }

    const submittedAtMs = new Date(queryState.submittedAt).getTime()
    const updateElapsed = () => {
      setElapsedSeconds(Math.max(0, Math.floor((Date.now() - submittedAtMs) / 1000)))
    }

    updateElapsed()
    const intervalId = setInterval(updateElapsed, 1000)
    return () => clearInterval(intervalId)
  }, [isInFlight, queryState?.submittedAt])

  if (!queryState) {
    return null
  }

  const { id, result, errorMessage } = queryState

  return (
    <div className="queryResult" data-status={status}>
      <div className="queryResultHeader">
        <span className={`statusBadge statusBadge-${status}`}>{STATUS_LABELS[status] || status}</span>
        {isInFlight && <span className="elapsedTime">{formatElapsed(elapsedSeconds)}</span>}
      </div>

      {status === 'submitting' && <p className="queryResultHint">Sending the query to the backend…</p>}

      {status === 'translation_failed' && (
        <p className="queryResultError">
          The query could not be translated into an executable form. (failure id: {id})
        </p>
      )}

      {status === 'pending' && (
        <p className="queryResultHint">Query cached, waiting for execution to start.</p>
      )}

      {status === 'running' && <p className="queryResultHint">Query is executing against the database.</p>}

      {status === 'failed' && <p className="queryResultError">{errorMessage || 'The query failed to execute.'}</p>}

      {status === 'completed' && <QueryResultTable result={result} />}
    </div>
  )
}

export default QueryResult
