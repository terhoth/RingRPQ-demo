# Ring-RPQ-demo

**Setting this up for the first time?** See [SETUP.md](SETUP.md) for the full install and
build guide (Linux/WSL primary, macOS in an appendix), and [OPERATION.md](OPERATION.md) for
running the stack day to day.

## Ring-RPQ

Graph compression and query evaluation algorithms.

To build the code:
1. Install the **extended** SDSL library from <https://github.com/adriangbrandon/sdsl-lite>.
   This fork is required — vanilla SDSL-lite is missing symbols this codebase depends on
   (`wm_int::unmark`, `initializable_array`, ...) and will not build. On a current toolchain
   its `install.sh` needs `CXXFLAGS="-Wno-template-body" CMAKE_POLICY_VERSION_MINIMUM=3.5`;
   see [SETUP.md](SETUP.md).
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

### Data

The executable `yago-prepare` can be used to import data from turtle (.ttl) format, with the restriction that turtle's semicolon and bracket shorthands should not be used, i.e. each line should be an explicit triple or prefix definition. It is confirmed to work at least with the YAGO 4.6 knowledge base's fact file.

After the data is imported into e.g. Ring-RPQ/data/example/example.dat, it can be optimized by calling `./sort-dict-bfs ../data/example/example.dat` in the build directory.

## XPathRingRPQ

ANTLR4-based translation from an extended form of XPath to the syntax accepted by Ring-RPQ.

To compile the code and translate a query:
1. `mvn compile`
2. `mvn spring-boot:start`
3. `curl http://localhost:8080/translate?query=XPATH_EXPRESSION`
4. `mvn spring-boot:stop`

Port 8080 is the default. It, and the backend API's 8000, are set in
[ports.conf](ports.conf) at the repo root — see [OPERATION.md](OPERATION.md#ports).
