/*
 * File: worker_db.cpp
 * Description: libpqxx-backed implementation of worker_db.hpp. Compiled
 *   under -std=c++17 (see worker_db.hpp for why this is a separate
 *   translation unit from worker.cpp).
 */

#include "worker_db.hpp"

#include <pqxx/pqxx>

struct DbHandle {
    pqxx::connection conn;
};

DbHandle* db_connect(const std::string& conninfo) { return new DbHandle{pqxx::connection(conninfo)}; }

void db_disconnect(DbHandle* handle) { delete handle; }

void db_expire_timed_out_pending(DbHandle& handle) {
    pqxx::work txn{handle.conn};
    txn.exec(
        "UPDATE query_cache "
        "SET status = 'failed', error_message = 'Timeout reached', completed_at = now() "
        "WHERE status = 'pending' "
        "  AND time_out_limit > 0 "
        "  AND submitted_at + (time_out_limit || ' seconds')::interval < now()");
    txn.commit();
}

// FOR UPDATE SKIP LOCKED guards against two workers accidentally pointed at
// the same database_name double-claiming the same row (workers for
// *different* databases never contend for the same rows in the first place,
// since they're partitioned by the WHERE clause below).
bool db_claim_next_pending(DbHandle& handle, const std::string& database_name, ClaimedRow& out) {
    pqxx::work txn{handle.conn};
    auto r = txn.exec(
        "UPDATE query_cache SET status = 'running' "
        "WHERE id = ("
        "  SELECT id FROM query_cache WHERE database_name = $1 AND status = 'pending' "
        "  ORDER BY submitted_at ASC LIMIT 1 "
        "  FOR UPDATE SKIP LOCKED"
        ") "
        "RETURNING id, database_name, translated_query, max_number_of_results, "
        "  CASE WHEN time_out_limit > 0 THEN "
        "    GREATEST(0, EXTRACT(EPOCH FROM (submitted_at + (time_out_limit || ' seconds')::interval - now())))"
        "  ELSE NULL END AS remaining_seconds",
        pqxx::params{database_name});
    txn.commit();

    if (r.empty()) return false;

    auto row = r[0];
    out.id = row["id"].as<long long>();
    out.database_name = row["database_name"].as<std::string>();
    out.translated_query = row["translated_query"].as<std::string>();
    out.max_number_of_results = row["max_number_of_results"].as<int>();
    out.has_timeout = !row["remaining_seconds"].is_null();
    out.remaining_seconds = out.has_timeout ? row["remaining_seconds"].as<double>() : 0.0;
    return true;
}

void db_finalize_row(DbHandle& handle, long long id, const std::string& status, const std::string* result_json,
                      const std::string* error_message) {
    pqxx::work txn{handle.conn};
    auto opt_result = result_json ? std::optional<std::string>(*result_json) : std::nullopt;
    auto opt_error = error_message ? std::optional<std::string>(*error_message) : std::nullopt;
    txn.exec(
        "UPDATE query_cache SET status = $1, result = $2::jsonb, error_message = $3, completed_at = now() "
        "WHERE id = $4",
        pqxx::params{status, opt_result, opt_error, id});
    txn.commit();
}

bool db_try_reuse_cache(DbHandle& handle, const std::string& translated_query, const std::string& database_name,
                         int effective_max, std::string& out_result_json) {
    pqxx::work txn{handle.conn};
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
        pqxx::params{translated_query, database_name, effective_max});
    txn.commit();

    if (r.empty()) return false;
    out_result_json = r[0][0].as<std::string>();
    return true;
}
