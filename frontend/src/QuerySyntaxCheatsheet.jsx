import './QuerySyntaxCheatsheet.css'

const QuerySyntaxCheatsheet = () => (
  <div className="querySyntaxCheatsheet">
    <h2>Query syntax</h2>
    <p>
      The query syntax is based on a fragment of the XML query language XPath, with some
      modifications to better support graph database queries. The relevant part in the{' '}
      <a href="https://www.w3.org/TR/1999/REC-xpath-19991116" target="_blank" rel="noopener noreferrer">
        XPath specification
      </a>{' '}
      is section 2, "Location Paths". Note that as the data structure of a graph is not
      hierarchic like XML documents, the semantics are slightly different.
    </p>

    <h3>Summary of exceptions to XPath</h3>
    <ul>
      <li>vertex and edge steps alternate</li>
      <li>
        new axes <code>reverse</code>, <code>transitive</code>, <code>transitive-or-self</code>,{' '}
        <code>reverse-transitive</code>, <code>reverse-transitive-or-self</code>
      </li>
      <li>predicates must be either location paths or comparisons of an attribute against a literal</li>
      <li>no document-order, abbreviated steps, or arithmetic expressions</li>
    </ul>

    <h3>More detailed explanation</h3>
    <p>
      There are two kinds of node steps: vertex steps and edge steps. The first step in the query
      is assumed to be a vertex, and after that edges and vertices alternate. (For paths inside
      predicates, the first step is an edge. For clarity, the examples below write node names as
      capitalised and edge names in uppercase. The wildcard <code>*</code> can be used for both
      vertices and edges. For example, <code>Earth/*/*/*/*</code> matches any vertex reachable
      from Earth with <strong>two</strong> edges.
    </p>
    <p>
      There are also new axes for transitive closure of edges and travelling in inverse
      direction: <code>transitive</code>, <code>reverse</code>, and the variants{' '}
      <code>transitive-or-self</code>, <code>reverse-transitive</code>,{' '}
      <code>reverse-transitive-or-self</code>. These should be applied to edge steps, for example{' '}
      <code>*/AUTHORS/reverse::AUTHORS/*</code> matches coauthors. Alternatively the XPath axes{' '}
      <code>descendant</code>, <code>parent</code>, <code>descendant-or-self</code>,{' '}
      <code>ancestor</code>, and <code>ancestor-or-self</code> could be used equivalently, but
      this is not recommended as their interpretation is not compatible with XPath.
    </p>
    <p>
      Predicates must either be location paths, or simple attribute value tests that consist of
      the attribute's name, a comparison operator, and the value. For example,{' '}
      <code>*[LIKES/Climbing]/KNOWS/*[LIKES/Climbing]</code> matches acquaintances who share a
      passion to climbing, and <code>*/HAS[@amount = 4]/Leg</code> matches anything that has four
      legs. Possible comparison operators are <code>=</code> <code>&lt;</code> <code>&gt;</code>{' '}
      <code>&lt;=</code> <code>&gt;=</code>.
    </p>

    <p>Further restrictions to the query syntax:</p>
    <ul>
      <li>
        No document-order specific features: this includes in particular the axes{' '}
        <code>following</code> and <code>preceding</code>
      </li>
      <li>No abbreviated steps</li>
      <li>No arithmetic expressions or functions</li>
    </ul>
  </div>
)

export default QuerySyntaxCheatsheet
