/*
 * File: worker.cpp
 * Description:
 *   Long-running daemon that drains the `query_cache` table (see
 *   backend/app/models.py) from oldest pending row to newest, executing each
 *   one against the existing `query-index-attr` CLI and writing the result
 *   back. It never touches the RPQ evaluation code itself — it only
 *   orchestrates: claim a row, maybe reuse a cached answer, otherwise write a
 *   one-line temp query file and spawn query-index-attr as a subprocess,
 *   parse its stdout, and write the outcome back to Postgres.
 *
 *   Must be run with its working directory set to Ring-RPQ/ (matches how
 *   query-index-attr itself is documented to be invoked): it resolves
 *   ./build/query-index-attr, ./data/<database_name> and ./tmp/ relative to
 *   the current directory rather than the executable's own location.
 *
 *   DB connection string: first argv[1], then $RING_RPQ_DB_CONNINFO, else a
 *   default matching backend/.env's credentials (libpq connection URI, not
 *   SQLAlchemy's "+psycopg2" form).
 */

#include <pqxx/pqxx>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr int DEFAULT_MAX_RESULTS = 2000;
constexpr int POLL_INTERVAL_SECONDS = 1;

const fs::path DATA_DIR = "data";
const fs::path TMP_DIR = "tmp";
const std::string QUERY_INDEX_ATTR_PATH = "./build/query-index-attr";

volatile std::sig_atomic_t g_running = 1;

void handle_shutdown_signal(int) { g_running = 0; }

// ---------------------------------------------------------------------------
// A row claimed off query_cache, plus the deadline derived from its
// time_out_limit (seconds remaining from *submission*, not from claim time —
// a query that sat pending for a while has already burned part of its
// budget).
// ---------------------------------------------------------------------------
struct ClaimedRow {
    long long id = 0;
    std::string database_name;
    std::string translated_query;
    int max_number_of_results = 0;
    std::optional<double> remaining_seconds;  // nullopt = no timeout (time_out_limit == 0)
};

struct SubprocessResult {
    bool spawn_failed = false;
    bool timed_out = false;
    std::string output;  // combined stdout+stderr
};

struct ParsedOutput {
    bool success = false;
    std::vector<std::pair<std::string, std::string>> rows;
    bool truncated = false;
    std::string error_message;
};

// RAII cleanup for the per-query temp file — guarantees removal on every
// return path (success, failure, timeout, exception) without duplicating
// cleanup calls at each one.
struct TempFileGuard {
    fs::path path;
    explicit TempFileGuard(fs::path p) : path(std::move(p)) {}
    ~TempFileGuard() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// ---------------------------------------------------------------------------
// DB access
// ---------------------------------------------------------------------------

std::string resolve_conninfo(int argc, char** argv) {
    if (argc > 1) return argv[1];
    if (const char* env = std::getenv("RING_RPQ_DB_CONNINFO")) return env;
    return "postgresql://ringrpq:ringrpq@localhost:5432/ringrpq";
}

// Bulk-expires pending rows whose time_out_limit has elapsed since
// submission, without ever putting them through 'running'.
void expire_timed_out_pending(pqxx::connection& conn) {
    pqxx::work txn{conn};
    txn.exec(
        "UPDATE query_cache "
        "SET status = 'failed', error_message = 'Timeout reached', completed_at = now() "
        "WHERE status = 'pending' "
        "  AND time_out_limit > 0 "
        "  AND submitted_at + (time_out_limit || ' seconds')::interval < now()");
    txn.commit();
}

// Atomically claims the oldest pending row (single worker, so plain
// UPDATE ... WHERE id = (SELECT ...) is enough — no SKIP LOCKED needed).
// remaining_seconds is computed in SQL so the worker never has to parse a
// timestamptz itself.
std::optional<ClaimedRow> claim_next_pending(pqxx::connection& conn) {
    pqxx::work txn{conn};
    auto r = txn.exec(
        "UPDATE query_cache SET status = 'running' "
        "WHERE id = ("
        "  SELECT id FROM query_cache WHERE status = 'pending' "
        "  ORDER BY submitted_at ASC LIMIT 1"
        ") "
        "RETURNING id, database_name, translated_query, max_number_of_results, "
        "  CASE WHEN time_out_limit > 0 THEN "
        "    GREATEST(0, EXTRACT(EPOCH FROM (submitted_at + (time_out_limit || ' seconds')::interval - now())))"
        "  ELSE NULL END AS remaining_seconds");
    txn.commit();

    if (r.empty()) return std::nullopt;

    auto row = r[0];
    ClaimedRow claimed;
    claimed.id = row["id"].as<long long>();
    claimed.database_name = row["database_name"].as<std::string>();
    claimed.translated_query = row["translated_query"].as<std::string>();
    claimed.max_number_of_results = row["max_number_of_results"].as<int>();
    if (!row["remaining_seconds"].is_null()) {
        claimed.remaining_seconds = row["remaining_seconds"].as<double>();
    }
    return claimed;
}

void finalize_row(pqxx::connection& conn, long long id, const std::string& status,
                   std::optional<std::string> result_json, std::optional<std::string> error_message) {
    pqxx::work txn{conn};
    txn.exec(
        "UPDATE query_cache SET status = $1, result = $2::jsonb, error_message = $3, completed_at = now() "
        "WHERE id = $4",
        pqxx::params{status, result_json, error_message, id});
    txn.commit();
}

// Looks for the newest completed row for the same (database_name,
// translated_query) whose stored result has at least `effective_max` rows,
// or is itself the complete (non-truncated) answer set — either way it can
// serve this request. Trimming to the first `effective_max` rows and
// recomputing the truncated flag is done in SQL via jsonb functions, so this
// process never needs to parse JSON on the C++ side.
std::optional<std::string> try_reuse_cache(pqxx::connection& conn, const ClaimedRow& row, int effective_max) {
    pqxx::work txn{conn};
    auto r = txn.exec(
        "SELECT jsonb_build_object("
        "  'columns', result->'columns',"
        "  'rows', COALESCE("
        "    (SELECT jsonb_agg(elem ORDER BY idx) "
        "     FROM jsonb_array_elements(result->'rows') WITH ORDINALITY AS t(elem, idx) "
        "     WHERE idx <= $3::int),"
        "    '[]'::jsonb"
        "  ),"
        "  'truncated', COALESCE((result->>'truncated')::boolean, false) "
        "               OR (jsonb_array_length(result->'rows') > $3::int)"
        ") AS trimmed_result "
        "FROM query_cache "
        "WHERE translated_query_hash = sha256($1::bytea) "
        "  AND database_name = $2 "
        "  AND status = 'completed' "
        "  AND ("
        "    NOT COALESCE((result->>'truncated')::boolean, false) "
        "    OR jsonb_array_length(result->'rows') >= $3::int"
        "  ) "
        "ORDER BY submitted_at DESC LIMIT 1",
        pqxx::params{row.translated_query, row.database_name, effective_max});
    txn.commit();

    if (r.empty()) return std::nullopt;
    return r[0][0].as<std::string>();
}

// ---------------------------------------------------------------------------
// Subprocess execution
// ---------------------------------------------------------------------------

// Forks + execs argv_strs[0] with the rest of argv_strs as arguments, capturing
// combined stdout+stderr. If timeout_seconds is set and elapses before the
// child exits, SIGKILLs it and reports timed_out=true.
SubprocessResult run_with_timeout(const std::vector<std::string>& argv_strs,
                                   std::optional<double> timeout_seconds) {
    SubprocessResult result;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        result.spawn_failed = true;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        result.spawn_failed = true;
        return result;
    }

    if (pid == 0) {
        // Child: redirect stdout+stderr into the pipe, then exec.
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        std::vector<char*> exec_argv;
        exec_argv.reserve(argv_strs.size() + 1);
        for (auto& s : argv_strs) exec_argv.push_back(const_cast<char*>(s.c_str()));
        exec_argv.push_back(nullptr);

        execv(argv_strs[0].c_str(), exec_argv.data());
        _exit(127);  // execv only returns on failure
    }

    // Parent
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

    auto start = std::chrono::steady_clock::now();
    int status = 0;
    bool child_reaped = false;

    auto drain_available = [&]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
            result.output.append(buf, static_cast<size_t>(n));
        }
    };

    while (true) {
        struct pollfd pfd { pipefd[0], POLLIN, 0 };
        poll(&pfd, 1, 100);  // 100ms tick, just to re-check the deadline/child state
        if (pfd.revents & (POLLIN | POLLHUP)) drain_available();

        pid_t wp = waitpid(pid, &status, WNOHANG);
        if (wp == pid) {
            drain_available();
            child_reaped = true;
            break;
        }

        if (timeout_seconds.has_value()) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= *timeout_seconds) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                result.timed_out = true;
                child_reaped = true;
                break;
            }
        }
    }

    close(pipefd[0]);
    (void)child_reaped;
    return result;
}

// ---------------------------------------------------------------------------
// Output parsing — see Ring-RPQ/src/query-index-attr.cpp for the exact
// format being parsed here. Only ever invoked with a single-query temp file,
// so there's exactly one "<n>;<count>;<time>" line to find, and everything
// after "Results:" (if present) is the answer set.
// ---------------------------------------------------------------------------

ParsedOutput parse_query_index_attr_output(const std::string& combined_output) {
    ParsedOutput out;

    std::vector<std::string> lines;
    {
        std::istringstream iss(combined_output);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
    }

    static const std::regex count_line_re(R"(^\d+;(\d+);\d+$)");
    static const std::regex result_line_re(R"(^  (.+) --> (.+)$)");

    long long count = -1;
    int results_idx = -1;
    std::string parse_error_line;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (count == -1 && std::regex_match(lines[i], m, count_line_re)) {
            count = std::stoll(m[1]);
        }
        if (parse_error_line.empty() && lines[i].find("Parse error") != std::string::npos) {
            parse_error_line = lines[i];
        }
        if (lines[i] == "Results:") {
            results_idx = static_cast<int>(i);
        }
    }

    if (!parse_error_line.empty()) {
        out.success = false;
        out.error_message = parse_error_line;
        return out;
    }

    if (count == -1) {
        out.success = false;
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (!it->empty()) {
                out.error_message = *it;
                break;
            }
        }
        if (out.error_message.empty()) out.error_message = "query-index-attr produced no output";
        return out;
    }

    out.success = true;
    if (count == 0 || results_idx == -1) return out;  // empty answer set

    for (size_t i = static_cast<size_t>(results_idx) + 1; i < lines.size(); ++i) {
        if (lines[i] == "  ...") {
            out.truncated = true;
            break;
        }
        std::smatch m;
        if (std::regex_match(lines[i], m, result_line_re)) {
            out.rows.emplace_back(m[1].str(), m[2].str());
        }
    }

    return out;
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
    std::string out = R"({"columns":["subject","object"],"rows":[)";
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i) out += ",";
        out += "[\"" + json_escape(rows[i].first) + "\",\"" + json_escape(rows[i].second) + "\"]";
    }
    out += "],\"truncated\":";
    out += (truncated ? "true" : "false");
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// Per-row processing
// ---------------------------------------------------------------------------

void process_row(pqxx::connection& conn, const ClaimedRow& row) {
    // Row's own deadline may have already passed while it was waiting to be
    // claimed (race with expire_timed_out_pending, or just queued too long).
    if (row.remaining_seconds.has_value() && *row.remaining_seconds <= 0.0) {
        finalize_row(conn, row.id, "failed", std::nullopt, std::string("Timeout reached"));
        std::cout << "ring-rpq-worker: row " << row.id << " timed out before execution" << std::endl;
        return;
    }

    int effective_max = row.max_number_of_results > 0 ? row.max_number_of_results : DEFAULT_MAX_RESULTS;

    if (auto cached = try_reuse_cache(conn, row, effective_max)) {
        finalize_row(conn, row.id, "completed", *cached, std::nullopt);
        std::cout << "ring-rpq-worker: row " << row.id << " completed from cache" << std::endl;
        return;
    }

    fs::path temp_path = TMP_DIR / ("query_" + std::to_string(row.id) + ".tsv");
    TempFileGuard guard(temp_path);

    try {
        std::ofstream ofs(temp_path);
        if (!ofs) throw std::runtime_error("could not create " + temp_path.string());
        ofs << row.translated_query << "\n";
    } catch (const std::exception& e) {
        finalize_row(conn, row.id, "failed", std::nullopt, std::string("Internal error: ") + e.what());
        return;
    }

    fs::path index_path = DATA_DIR / row.database_name;
    std::vector<std::string> argv_strs = {QUERY_INDEX_ATTR_PATH, index_path.string(), temp_path.string(),
                                           std::to_string(effective_max)};

    SubprocessResult sub = run_with_timeout(argv_strs, row.remaining_seconds);

    if (sub.spawn_failed) {
        finalize_row(conn, row.id, "failed", std::nullopt, std::string("Failed to launch query-index-attr"));
        return;
    }
    if (sub.timed_out) {
        finalize_row(conn, row.id, "failed", std::nullopt, std::string("Timeout reached"));
        std::cout << "ring-rpq-worker: row " << row.id << " timed out during execution" << std::endl;
        return;
    }

    ParsedOutput parsed = parse_query_index_attr_output(sub.output);
    if (!parsed.success) {
        finalize_row(conn, row.id, "failed", std::nullopt, parsed.error_message);
        std::cout << "ring-rpq-worker: row " << row.id << " failed: " << parsed.error_message << std::endl;
        return;
    }

    std::string result_json = build_result_json(parsed.rows, parsed.truncated);
    finalize_row(conn, row.id, "completed", result_json, std::nullopt);
    std::cout << "ring-rpq-worker: row " << row.id << " completed (" << parsed.rows.size() << " rows"
              << (parsed.truncated ? ", truncated" : "") << ")" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    std::string conninfo = resolve_conninfo(argc, argv);

    std::error_code ec;
    fs::create_directories(TMP_DIR, ec);

    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    std::unique_ptr<pqxx::connection> conn;
    try {
        conn = std::make_unique<pqxx::connection>(conninfo);
    } catch (const std::exception& e) {
        std::cerr << "ring-rpq-worker: failed to connect to database: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "ring-rpq-worker: connected, polling query_cache every " << POLL_INTERVAL_SECONDS
              << "s (run from Ring-RPQ/ so ./build and ./data resolve)" << std::endl;

    while (g_running) {
        try {
            expire_timed_out_pending(*conn);
            auto claimed = claim_next_pending(*conn);
            if (!claimed) {
                std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SECONDS));
                continue;
            }
            process_row(*conn, *claimed);
        } catch (const std::exception& e) {
            std::cerr << "ring-rpq-worker: error in main loop: " << e.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL_SECONDS));
        }
    }

    std::cout << "ring-rpq-worker: shutting down" << std::endl;
    return 0;
}
