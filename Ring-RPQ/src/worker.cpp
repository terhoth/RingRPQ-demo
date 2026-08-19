/*
 * File: worker.cpp
 * Description:
 *   Long-running, single-database daemon that drains the `query_cache` table
 *   (see backend/app/models.py) of rows for its one `database_name`, oldest
 *   first. Loads that graph's index exactly once at startup via
 *   ring_rpq_attr_bfs::load(), then repeatedly calls ::evaluate() in-process
 *   for each claimed row — no subprocess, no temp files, no CLI involved.
 *   Since a worker only ever holds one graph in memory, serving N databases
 *   means running N instances of this binary, each pointed at a different
 *   database_name; they share the same query_cache table and each only
 *   claims rows matching its own database_name.
 *
 *   Postgres access is delegated to worker_db.hpp/.cpp — a separate
 *   translation unit compiled under C++17 for libpqxx, kept out of this one
 *   because ring_rpq_attr.hpp doesn't compile under C++17 (see
 *   worker_db.hpp for why). This file compiles under the project's normal
 *   C++11 flags, same as every other Ring-RPQ target — no std::optional,
 *   std::filesystem, or other post-C++11 standard library features.
 *
 *   Must be run with its working directory set to Ring-RPQ/: it resolves
 *   ./data/<database_name> relative to the current directory.
 *
 *   Usage: ring-rpq-worker <database_name> [conninfo]
 *   DB connection string: argv[2] if given, then $RING_RPQ_DB_CONNINFO, else
 *   a default matching backend/.env's credentials (libpq connection URI, not
 *   SQLAlchemy's "+psycopg2" form).
 */

#include "ring_rpq_attr.hpp"
#include "worker_db.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int DEFAULT_MAX_RESULTS = 2000;
constexpr int POLL_INTERVAL_SECONDS = 1;

const std::string DATA_DIR = "data";

volatile std::sig_atomic_t g_running = 1;

void handle_shutdown_signal(int) { g_running = 0; }

std::string resolve_conninfo(int argc, char** argv) {
    if (argc > 2) return argv[2];
    if (const char* env = std::getenv("RING_RPQ_DB_CONNINFO")) return env;
    return "postgresql://ringrpq:ringrpq@localhost:5432/ringrpq";
}

// ---------------------------------------------------------------------------
// Result JSON construction — {"columns": ["subject","object"], "rows": [...],
// "truncated": bool}. Matches what frontend/src/QueryResult.jsx expects.
// ---------------------------------------------------------------------------

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string build_result_json(const std::vector<std::pair<std::string, std::string>>& rows, bool truncated) {
    std::string out = "{\"columns\":[\"subject\",\"object\"],\"rows\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) out += ",";
        out += "[\"" + json_escape(rows[i].first) + "\",\"" + json_escape(rows[i].second) + "\"]";
    }
    out += "],\"truncated\":";
    out += (truncated ? "true" : "false");
    out += "}";
    return out;
}

// Ring-RPQ's Error enum (declared in parse_query_attr.cpp, pulled in via
// ring_rpq_attr.hpp) has no built-in message text — evaluate() just returns
// the enum value — so this is our own mapping for error_message.
std::string describe_error(Error e) {
    switch (e) {
        case Error::MismatchedAngle: return "Mismatched angle brackets";
        case Error::MismatchedSquare: return "Mismatched square brackets";
        case Error::MisplacedSquare: return "Misplaced square brackets";
        case Error::UnknownPredicate: return "Unknown predicate";
        case Error::UnknownAttribute: return "Unknown attribute";
        case Error::InvalidCondition: return "Invalid attribute condition";
        case Error::InvalidCurlyBrackets: return "Mismatched or misplaced curly brackets";
        case Error::MissingEndpoint: return "Could not find subject and/or object";
        default: return "Query evaluation failed";
    }
}

// ---------------------------------------------------------------------------
// Per-row processing
// ---------------------------------------------------------------------------

void process_row(DbHandle& db, ring_rpq_attr_bfs& graph, const ClaimedRow& row) {
    // Row's own deadline may have already passed while it was waiting to be
    // claimed (race with db_expire_timed_out_pending, or just queued too
    // long).
    if (row.has_timeout && row.remaining_seconds <= 0.0) {
        std::string msg = "Timeout reached";
        db_finalize_row(db, row.id, "failed", nullptr, &msg);
        std::cout << "ring-rpq-worker: row " << row.id << " timed out before execution" << std::endl;
        return;
    }

    int effective_max = row.max_number_of_results > 0 ? row.max_number_of_results : DEFAULT_MAX_RESULTS;

    std::string cached_json;
    if (db_try_reuse_cache(db, row.translated_query, row.database_name, effective_max, cached_json)) {
        db_finalize_row(db, row.id, "completed", &cached_json, nullptr);
        std::cout << "ring-rpq-worker: row " << row.id << " completed from cache" << std::endl;
        return;
    }

    // evaluate()'s timeout is milliseconds and only affects whether it
    // *reports* Error::TimedOut — the actual search loops self-limit to a
    // fixed ~600ms per phase (TIME_OUT in query_config.hpp) regardless of
    // what's passed here, so this can't make a query run away. No timeout
    // (time_out_limit == 0) means "don't report a timeout for our own
    // budget" — pass the largest representable value.
    unsigned int timeout_ms = row.has_timeout ? static_cast<unsigned int>(row.remaining_seconds * 1000.0)
                                               : std::numeric_limits<unsigned int>::max();

    std::vector<std::pair<std::string, std::string>> output;
    Error error = graph.evaluate(row.translated_query, output, static_cast<uint64_t>(effective_max), timeout_ms);

    if (error == Error::TimedOut) {
        std::string msg = "Timeout reached";
        db_finalize_row(db, row.id, "failed", nullptr, &msg);
        std::cout << "ring-rpq-worker: row " << row.id << " timed out during execution" << std::endl;
        return;
    }
    if (error != Error::None && error != Error::ReachedBound) {
        std::string message = describe_error(error);
        db_finalize_row(db, row.id, "failed", nullptr, &message);
        std::cout << "ring-rpq-worker: row " << row.id << " failed: " << message << std::endl;
        return;
    }

    bool truncated = (error == Error::ReachedBound);
    std::string result_json = build_result_json(output, truncated);
    db_finalize_row(db, row.id, "completed", &result_json, nullptr);
    std::cout << "ring-rpq-worker: row " << row.id << " completed (" << output.size() << " rows"
              << (truncated ? ", truncated" : "") << ")" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <database_name> [conninfo]" << std::endl;
        std::cerr << "  database_name is relative to Ring-RPQ/data/, e.g. test_correctness/test.dat"
                  << std::endl;
        return 1;
    }
    std::string database_name = argv[1];
    std::string conninfo = resolve_conninfo(argc, argv);

    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    ring_rpq_attr_bfs graph;
    try {
        std::string index_path = DATA_DIR + "/" + database_name;
        auto load_start = std::chrono::steady_clock::now();
        graph.load(index_path);
        auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - load_start)
                           .count();
        std::cout << "ring-rpq-worker: loaded " << database_name << " (" << graph.n_triples() << " triples, "
                  << load_ms << " ms)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ring-rpq-worker: failed to load index for " << database_name << ": " << e.what()
                  << std::endl;
        return 1;
    }

    DbHandle* db = nullptr;
    try {
        db = db_connect(conninfo);
    } catch (const std::exception& e) {
        std::cerr << "ring-rpq-worker: failed to connect to database: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "ring-rpq-worker: connected, polling query_cache for database_name='" << database_name
              << "' every " << POLL_INTERVAL_SECONDS << "s (run from Ring-RPQ/ so ./data resolves)" << std::endl;

    while (g_running) {
        try {
            db_expire_timed_out_pending(*db);
            ClaimedRow claimed;
            if (!db_claim_next_pending(*db, database_name, claimed)) {
                std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SECONDS));
                continue;
            }
            process_row(*db, graph, claimed);
        } catch (const std::exception& e) {
            std::cerr << "ring-rpq-worker: error in main loop: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SECONDS));
        }
    }

    std::cout << "ring-rpq-worker: shutting down" << std::endl;
    db_disconnect(db);
    return 0;
}
