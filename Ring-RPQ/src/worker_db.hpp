/*
 * File: worker_db.hpp
 * Description:
 *   Postgres access for ring-rpq-worker, isolated behind an opaque handle so
 *   this header can be included from worker.cpp without pulling in
 *   <pqxx/pqxx> there. That split exists purely because of a toolchain
 *   conflict, not a design preference: libpqxx 7.10 needs -std=c++17, but
 *   ring_rpq_attr.hpp (via the SDSL fork's wm_int.hpp -> init_array.hpp)
 *   uses the `register` storage-class specifier, which is a hard compile
 *   error under strict C++17 (Apple Clang) though only a deprecation
 *   warning under C++11 (this project's default, which worker.cpp uses).
 *   worker_db.cpp compiles under C++17. Because this header is included by
 *   both, it deliberately avoids anything C++17-only (std::optional,
 *   std::filesystem, ...) — plain pointers/bools stand in for "optional"
 *   here instead.
 */

#pragma once

#include <string>

struct DbHandle;

// Connects to Postgres; throws std::exception (via pqxx) on failure.
DbHandle* db_connect(const std::string& conninfo);
void db_disconnect(DbHandle* handle);

struct ClaimedRow {
    long long id = 0;
    std::string database_name;
    std::string translated_query;
    int max_number_of_results = 0;
    bool has_timeout = false;      // false <=> time_out_limit == 0 (no timeout)
    double remaining_seconds = 0;  // meaningful only if has_timeout
};

// Bulk-expires pending rows whose time_out_limit has elapsed since
// submission, without ever putting them through 'running'. Not scoped to
// any one database — a plain timestamp sweep needs no loaded index, so any
// running worker keeps every database's queue honest, not just its own.
void db_expire_timed_out_pending(DbHandle& handle);

// Atomically claims the oldest pending row for the given database_name.
// Returns false (leaving `out` untouched) if there's nothing pending.
bool db_claim_next_pending(DbHandle& handle, const std::string& database_name, ClaimedRow& out);

// result_json / error_message: pass nullptr for SQL NULL.
void db_finalize_row(DbHandle& handle, long long id, const std::string& status, const std::string* result_json,
                      const std::string* error_message);

// Looks for the newest completed row for the same (database_name,
// translated_query) whose stored result has at least `effective_max` rows,
// or is itself the complete (non-truncated) answer set. On a hit, fills
// `out_result_json` with the already-trimmed
// {"columns":...,"rows":...,"truncated":...} JSON text, ready to write
// straight into `result`, and returns true.
bool db_try_reuse_cache(DbHandle& handle, const std::string& translated_query, const std::string& database_name,
                         int effective_max, std::string& out_result_json);
