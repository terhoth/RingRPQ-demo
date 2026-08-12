# Ring-RPQ-Attr

**Note:** See the README file in adriangbrandon/Ring-RPQ for more information and instructions.

This repository modifies the original index structure and evaluation algorithms to support attributes on edges and nodes. It does not necessarily preserve the space and time guarantees of the original implementation. In practise unattributed queries don't seem to experience a significant slowdown, or can even be faster. 

The splitting optimizations from RingAB have not been implemented, as attributes would require rethinking how selectivity is estimated. However, especially queries containing intermediate constant nodes would likely benefit from such splitting.

The new/modified code files in `src` and `nfa` are suffixed with 'attr', except for src/test-correctness.cpp, src/load\_index\_util.cpp and nfa/include/attr\_info.hpp.

## Executables

The new executables compared to the original Ring-RPQ are:
* `build-index-attr <path_to_dat>`: analogous to `build-index-basic`.
* `query-index-attr <path_to_dat> <path_to_queries>`: analogous to `query-index-basic`, with two additional optional command-line arguments to specify the number of results to print and the maximum bound of results to search, respectively.
* `test-correctness <path_to_dat> <path_to_queries> <path_to_answers>`: runs the queries and compares the output to the results in the given answer file, reporting any deviations. A default test data and queries are included in data/test\_correctness.
* `find-paths <path_to_dat> <start_node> <path_length>`: prints all paths of the given length starting from the node with the given label.

## Data format

To construct the index, you need four files:

* `example.dat`: each row contains the integer id's of a subject, a predicate and an object, followed by edge attributes values, separated with whitespace. 
* `example.dat.SO`: each row contains the id and label of a node, followed by node attribute values, separated with whitespace.
* `example.dat.P`: each row contains the id and label of a predicate.
* `example.dat.A`: lists the edge and node attribute names. Use the headers `STRING`, `INTEGER`, `FLOAT`, `EDGE`, `NODE` to indicate the type of following attributes. The same attribute name should be listed only once.

Node and edge labels in .P and .SO must be enclosed in angle brackets and may not contain `<>[]{}?%&` or whitespace.

Unattributed indexes are also still supported, and can be constructed directly with `build-index-attr`.

### Attributes

The supported datatypes are strings, integers and floating point numbers. Missing attribute values are allowed, and can be represented in the .dat and .dat.SO files as an underscore `_`. Attribute names may not contain brackets or whitespace, and should not begin and end with an underscore.

## Query syntax

### Navigational

Queries consist of a subject, expression, and an object, separated by spaces. The subject and object can be variable, which is indicated by prefixing them with a question mark (the variable names don't matter), or they can be the label of a node. 

Examples: 
* variable endpoints: `?x <has> ?y`
* constant subject: `<Frog> <has> ?y`
* constant object: `?x <has> <Leg>`
* reversed edge: `?x <%has> ?y`
* wildcard edge: `<Frog> <_> <Leg>`
* concatenation: `?x <is>/<has> ?y`
* transitive closure: `?x (<has>)+ ?y`
* reflexive-transitive closure: `?x (<has>)* ?y`
* union: `?x <likes>|<hates> ?y`
* intermediate nodes: `?x <is>/<&Animal>/<has> ?y`

### Attributes

Queries may include attribute tests in square brackets. They are placed inside an angle-bracketed edge or node, after the label, e.g. `<has[amount = 100]>`. Wildcard edges can be used to match any edge with suitable attributes irrespective of label. For nodes, a similar effect is achieved by omitting the label. Several attribute tests can be applied to the same node or edge.

Examples:
* edge attributes: `?x <has[amount = 100]> ?y`
* node attributes: `?x <is>/<&[type = species]>/<has> ?y`
* several attributes: `?x <has[amount > 50][amount <= 100]> ?y`

Attribute tests must consist of an attribute name, an operator, and a value, separated with spaces. Possible operators are:
* comparison operators: `= != < <= > >=`
* string operators: `BEGINS ENDS PREFIX POSTFIX`
* float approximation: `ROUND` 

By default, attribute constaints will not match null values. To explicitly test nullness, use `[attr = _]` and `[attr != _]`. 

### Branching

The query syntax is extended with XPath-style branching, also known as nested regular expressions. Curly brackets can be placed inside an intermediate node to indicate branching. For example, the expression `<&{<likes>}>` matches all nodes from which there exist an edge labelled 'likes'. The branching subexpression follows the same syntax as the main expression, and can even contain further branches.
