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
 *   Resolves <data_dir>/<database_name>, where data_dir is (in order):
 *   $RING_RPQ_DATA_DIR if set; else the contents of data-dir.conf next to
 *   this binary's cwd (gitignored — a single-line path, same file
 *   run-workers.sh and the backend read, so the location only needs setting
 *   once); else "data". Both the file and the "data" fallback are relative
 *   to the current directory, so with neither override this must be run
 *   with its working directory set to Ring-RPQ/.
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
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int DEFAULT_MAX_RESULTS = 2000;
constexpr int POLL_INTERVAL_SECONDS = 1;

volatile std::sig_atomic_t g_running = 1;

void handle_shutdown_signal(int) { g_running = 0; }

std::string resolve_conninfo(int argc, char** argv) {
    if (argc > 2) return argv[2];
    if (const char* env = std::getenv("RING_RPQ_DB_CONNINFO")) return env;
    return "postgresql://ringrpq:ringrpq@localhost:5432/ringrpq";
}

const char* const DATA_DIR_CONFIG_FILE = "data-dir.conf";

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Reads the first non-empty, non-comment ('#') line of data-dir.conf, if
// present. Returns false if the file doesn't exist or has no usable line.
bool read_data_dir_config_file(std::string& out) {
    std::ifstream f(DATA_DIR_CONFIG_FILE);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        out = trimmed;
        return true;
    }
    return false;
}

// Database folder location, in order: $RING_RPQ_DATA_DIR if set; else
// data-dir.conf next to the cwd, if present (same file run-workers.sh and
// the backend read — set it once there instead of exporting the env var
// everywhere); else "data". The file and the "data" fallback are both
// resolved relative to the current directory.
std::string resolve_data_dir() {
    if (const char* env = std::getenv("RING_RPQ_DATA_DIR")) return env;
    std::string from_file;
    if (read_data_dir_config_file(from_file)) return from_file;
    return "data";
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

// Ring-RPQ node labels carry their enclosing angle brackets as part of the
// literal string (Ring-RPQ/README.md: labels "must be enclosed in angle
// brackets"), so evaluate() returns e.g. "<A>", not "A". Redundant once
// stored as a JSON string value, so it's stripped here, once, right before
// writing — not retroactively, so rows written before this change keep
// their brackets until re-executed.
std::string strip_angle_brackets(const std::string& s) {
    if (s.size() >= 2 && s.front() == '<' && s.back() == '>') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::string build_result_json(const std::vector<std::pair<std::string, std::string>>& rows, bool truncated) {
    std::string out = "{\"columns\":[\"subject\",\"object\"],\"rows\":[";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) out += ",";
        out += "[\"" + json_escape(strip_angle_brackets(rows[i].first)) + "\",\"" +
               json_escape(strip_angle_brackets(rows[i].second)) + "\"]";
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
        case Error::MismatchedAngle: return "Vertex or edge name contains < or >?";
        case Error::MismatchedSquare: return "Mismatched square brackets";
        case Error::MisplacedSquare: return "Misplaced square brackets";
        case Error::UnknownPredicate: return "Unknown edge name";
        case Error::UnknownAttribute: return "Unknown vertex name";
        case Error::InvalidCondition: return "Invalid attribute predicate";
        case Error::InvalidCurlyBrackets: return "Location path predicate on an edge step?";
        case Error::MissingEndpoint: return "Could not parse path endpoints";
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

    // evaluate()'s timeout (milliseconds) is now actually enforced by every
    // internal search loop (see check_timeout() in ring_rpq_attr.hpp), so
    // there's no engine-side self-limit left to fall back on. A row with no
    // user-specified deadline still needs a hard cap here, or a pathological
    // query can run the worker indefinitely — use the engine's own default.
    unsigned int timeout_ms = row.has_timeout ? static_cast<unsigned int>(row.remaining_seconds * 1000.0)
                                               : TIME_OUT;

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
        std::cerr << "  database_name is relative to the data dir (default: Ring-RPQ/data/, "
                     "override with $RING_RPQ_DATA_DIR), e.g. test_correctness/test.dat"
                  << std::endl;
        return 1;
    }
    std::string database_name = argv[1];
    std::string conninfo = resolve_conninfo(argc, argv);
    std::string data_dir = resolve_data_dir();

    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    ring_rpq_attr_bfs graph;
    try {
        std::string index_path = data_dir + "/" + database_name;
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
              << "' every " << POLL_INTERVAL_SECONDS << "s (data dir: " << data_dir << ")" << std::endl;

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
