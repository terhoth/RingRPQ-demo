# Ring-RPQ-demo

## Ring-RPQ

Graph compression and query evaluation algorithms.

To build the code:
1. Install an extended SDSL library from <https://github.com/adriangbrandon/sdsl-lite>
2. In `Ring-RPQ` create a subdirectory `build`
3. Run `cmake ..` and `make` in the build directory

To construct the index structure for a graph: (this may take hours if the graph is large)
1. Put the required `.dat` files in a directory, e.g. `Ring-RPQ/data/example/`
2. In the build directory, run `./build-index-attr ../data/example/example.dat`

To evaluate queries from a file `queries.tsv`:
1. In the build directory, run `./query-index-attr ../data/example/example.dat ../data/example/queries.tsv`

The public interface of `ring_rpq_attr` consist of four methods:
* `construct(string filename, bool verbose = false)`: builds a new index structure from data
* `save(string filename)`: stores the constructed index in same directory as filename
* `load(string filename bool verbose = false)`: loads the saved index so that it is ready for queries
* `evaluate(string query, vector<pair<string,string> output, int bound, int timeout)`: evaluates a query

For more information, see `Ring-RPQ/README.md`.

## XPathRingRPQ

ANTLR4-based translation from an extended form of XPath to the syntax accepted by Ring-RPQ.

To compile the code and translate a query:
1. `mvn compile`
2. `mvn spring-boot:start`
3. `curl http://localhost:8080/translate?query=XPATH_EXPRESSION`
4. `mvn spring-boot:stop`
