import QueryForm from './QueryForm'
import QuerySyntaxCheatsheet from './QuerySyntaxCheatsheet'
import './App.css'

function App() {
  return (
    <div className="appContainer">
      <h1>RingRPQ Query Console</h1>
      <div className="appLayout">
        <div className="queryConsole">
          <QueryForm />
        </div>
        <QuerySyntaxCheatsheet />
      </div>
    </div>
  )
}

export default App
