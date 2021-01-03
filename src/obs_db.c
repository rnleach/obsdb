/** \file obs_db.c
 *
 * \brief Internal implementation of the local portion of the ObsStore
 */
#include "obs_db.h"
#include "obs.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>

#include <sys/stat.h>

#include <sqlite3.h>

/** Retrieve the full path to a database file for the local store. */
static char const *
get_or_create_db_path(void)
{
    static char path[64] = {0};

    char const *home = getenv("HOME");
    StopIf(!home, exit(EXIT_FAILURE), "could not find user's home directory.");

    sprintf(path, "%s/.local/", home);

    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0774);
    }

    sprintf(path, "%s/.local/share/", home);
    if (stat(path, &st) == -1) {
        mkdir(path, 0774);
    }

    sprintf(path, "%s/.local/share/obsdb/", home);
    if (stat(path, &st) == -1) {
        mkdir(path, 0774);
    }

    sprintf(path, "%s/.local/share/obsdb/wxobs.sqlite", home);

    return path;
}

sqlite3 *
obs_db_open_create(void)
{
    sqlite3 *err_return = 0;
    sqlite3_stmt *statement = 0;
    sqlite3 *db = err_return;
    int res = SQLITE_OK;

    char const *path = get_or_create_db_path();

    res = sqlite3_open(path, &db);
    StopIf(res != SQLITE_OK, goto CLEAN_UP_AND_RETURN_ERROR, "unable to open download cache: %s",
           sqlite3_errstr(res));

    char *sql = "CREATE TABLE IF NOT EXISTS obs (                                     \n"
                "  site           TEXT    NOT NULL, -- Synoptic Labs API site id      \n"
                "  valid_time     INTEGER NOT NULL, -- unix time stamp of valid time. \n"
                "  t_f            REAL,             -- temperature in Fahrenheit      \n"
                "  precip_in_1hr  REAL,             -- precipitation in inches        \n"
                "  PRIMARY KEY (site, valid_time));                                   \n";

    res = sqlite3_prepare_v2(db, sql, -1, &statement, 0);
    StopIf(res != SQLITE_OK, goto CLEAN_UP_AND_RETURN_ERROR,
           "error preparing cache initialization sql: %s", sqlite3_errstr(res));

    res = sqlite3_step(statement);
    StopIf(res != SQLITE_DONE, goto CLEAN_UP_AND_RETURN_ERROR,
           "error executing cache initialization sql: %s", sqlite3_errstr(res));

    res = sqlite3_finalize(statement);
    StopIf(res != SQLITE_OK, goto CLEAN_UP_AND_RETURN_ERROR,
           "error finalizing cache initialization sql: %s", sqlite3_errstr(res));

    return db;

CLEAN_UP_AND_RETURN_ERROR:

    res = sqlite3_finalize(statement);
    if (res != SQLITE_OK) {
        fprintf(stderr, "error finalizing cache initialization sql: %s", sqlite3_errstr(res));
    }

    res = sqlite3_close(db);
    if (res != SQLITE_OK) {
        fprintf(stderr, "error closing sqlite3 database: %s", sqlite3_errstr(res));
    }
    return err_return;
}

int
obs_db_close(sqlite3 *db)
{
    int const err_return_val = -1;
    int const success_return_val = 0;

    int res = SQLITE_OK;

    time_t now = time(0);
    time_t too_old = now - 60 * 60 * 24 * 555; // About 555 days. That's over 1.5 years!

    char *sql = "DELETE FROM obs WHERE valid_time < ?";

    sqlite3_stmt *statement = 0;
    res = sqlite3_prepare_v2(db, sql, -1, &statement, 0);
    StopIf(res != SQLITE_OK, return err_return_val, "error preparing delete statement: %s",
           sqlite3_errstr(res));

    res = sqlite3_bind_int64(statement, 1, too_old);
    StopIf(res != SQLITE_OK, return err_return_val, "error binding init_time in delete: %s",
           sqlite3_errstr(res));

    res = sqlite3_step(statement);
    StopIf(res != SQLITE_ROW && res != SQLITE_DONE, return err_return_val,
           "error executing select sql: %s", sqlite3_errstr(res));

    res = sqlite3_finalize(statement);
    StopIf(res != SQLITE_OK, return err_return_val, "error finalizing delete statement: %s",
           sqlite3_errstr(res));

    res = sqlite3_close(db);
    StopIf(res != SQLITE_OK, return err_return_val, "error closing sqlite3 database: %s",
           sqlite3_errstr(res));

    return success_return_val;
}

/** Find out how many rows will be returned.
 *
 * \returns the number of rows that will be returned from a query for the same site and time range.
 * If there is an error it returns \c SIZE_MAX
 */
static size_t
obs_db_count_rows_in_range(sqlite3 *db, char const *const site, struct ObsTimeRange tr)
{
    sqlite3_stmt *statement = 0;

    char query[256] = {0};
    sprintf(query,
            "SELECT COUNT(valid_time) "
            "FROM obs "
            "WHERE site='%s' "
            "    AND valid_time >= %ld "
            "    AND valid_time <= %ld;",
            site, tr.start, tr.end);

    int rc = sqlite3_prepare_v2(db, query, -1, &statement, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error preparing select statement:\n     %s\n     %s",
           query, sqlite3_errstr(rc));

    rc = sqlite3_step(statement);
    StopIf(rc != SQLITE_ROW && rc != SQLITE_DONE, goto ERR_RETURN,
           "error executing select: %s\n %s\n", query, sqlite3_errstr(rc));

    int col_type = sqlite3_column_type(statement, 0);
    StopIf(col_type != SQLITE_INTEGER, goto ERR_RETURN, "impossible non-integer returned");

    static_assert(sizeof(sqlite3_int64) <= sizeof(size_t), "size_t too small");
    sqlite3_int64 count = sqlite3_column_int64(statement, 0);
    assert(count >= 0);

    sqlite3_finalize(statement);
    return count;

ERR_RETURN:
    sqlite3_finalize(statement);
    return SIZE_MAX;
}

static int
obs_db_have_inventory_step_row(sqlite3_stmt *statement, time_t t1[static 1])
{
    int rc = sqlite3_step(statement);
    StopIf(rc != SQLITE_ROW && rc != SQLITE_DONE, goto EARLY_RETURN, "error executing select: %s\n",
           sqlite3_errstr(rc));

    if (rc == SQLITE_DONE) {
        goto EARLY_RETURN;
    }

    int col_type = sqlite3_column_type(statement, 0);
    StopIf(col_type != SQLITE_INTEGER, goto EARLY_RETURN, "impossible non-integer returned");

    static_assert(sizeof(sqlite3_int64) <= sizeof(time_t), "time_t too small");
    sqlite3_int64 timestamp = sqlite3_column_int64(statement, 0);
    *t1 = timestamp;

    return rc;

EARLY_RETURN:

    *t1 = 0;
    return rc;
}

int
obs_db_have_inventory(sqlite3 *db, char const *const site, struct ObsTimeRange tr,
                      struct ObsTimeRange **missing_ranges, size_t *num_missing_ranges)
{
    assert(site && "null site");
    assert(tr.start < tr.end && "time range ends before it starts!");

    sqlite3_stmt *statement = 0;

    StopIf(missing_ranges && !num_missing_ranges, goto ERR_RETURN,
           "A pointer was supplied to return missing time ranges, but not a pointer\n"
           "for the number of missing time ranges: num_missing_ranges is NULL");

    size_t num_tr = 0;
    struct ObsTimeRange trs[100] = {0};

    char query[256] = {0};
    sprintf(query,
            "SELECT valid_time "
            "FROM obs "
            "WHERE site='%s' "
            "    AND valid_time >= %ld "
            "    AND valid_time <= %ld "
            "ORDER BY valid_time ASC",
            site, tr.start, tr.end);

    int rc = sqlite3_prepare_v2(db, query, -1, &statement, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error preparing select statement:\n     %s\n     %s",
           query, sqlite3_errstr(rc));

    time_t t1 = {0};

    rc = obs_db_have_inventory_step_row(statement, &t1);
    if (rc != SQLITE_ROW) {
        // There is no data in the database yet, so the whole time range is missing.
        trs[num_tr] = tr;
        num_tr++;
        goto ALLOCATE_AND_RETURN;
    }

    if (t1 > tr.start && difftime(t1, tr.start) > 4000.0) {
        // Missing a chunk at the beginning.
        trs[num_tr] = (struct ObsTimeRange){.start = tr.start, .end = t1};
        num_tr++;
    }

    while (true) {
        time_t t0 = t1;

        rc = obs_db_have_inventory_step_row(statement, &t1);
        if (rc != SQLITE_ROW || t1 == 0) {
            // We're done, clean up and prepare to check ending
            t1 = t0;
            break;
        }

        double gap_seconds = difftime(t1, t0);
        if (gap_seconds > 4000.0) {
            trs[num_tr] = (struct ObsTimeRange){.start = t0, .end = t1};
            num_tr++;
            if (num_tr >= sizeof(trs) / sizeof(trs[0])) {
                // We're out of space, do the best you can. This is crazy.
                goto ALLOCATE_AND_RETURN;
            }
        }
    }

    if (tr.end > t1 && difftime(tr.end, t1) > 4000.0) {
        // Missing a chunk at the end.
        trs[num_tr] = (struct ObsTimeRange){.start = t1, .end = tr.end};
        num_tr++;
    }

ALLOCATE_AND_RETURN:

    sqlite3_finalize(statement);

    if (num_tr == 0) {
        // There was no missing time ranges, nothing to allocate
        *num_missing_ranges = 0;
        *missing_ranges = 0;
        return 1;
    } else {
        *num_missing_ranges = num_tr;
        *missing_ranges = calloc(num_tr, sizeof(struct ObsTimeRange));
        StopIf(!*missing_ranges, goto ERR_RETURN, "out of memory");
        memcpy(*missing_ranges, trs, num_tr * sizeof(trs[0]));
        return 0;
    }

ERR_RETURN:

    sqlite3_finalize(statement);
    *num_missing_ranges = 0;
    *missing_ranges = 0;
    return -1;
}

static size_t
obs_db_query_calculate_num_results(struct ObsTimeRange tr, unsigned window_increment)
{
    double diff_seconds = difftime(tr.end, tr.start);
    StopIf(diff_seconds < 0.0, return SIZE_MAX, "backwards ObsTimeRange");

    double num_results = (diff_seconds + 1) / HOURSEC / window_increment;
    StopIf(num_results >= (double)SIZE_MAX / 2.0, return SIZE_MAX,
           "too many results, something wrong");

    return (size_t)num_results;
}

static int
obs_db_query_temperatures_get_hourlies_step_row(sqlite3_stmt *statement,
                                                struct ObsTemperature ob[static 1])
{
    int rc = sqlite3_step(statement);
    StopIf(rc != SQLITE_ROW && rc != SQLITE_DONE, goto EARLY_RETURN, "error executing select: %s\n",
           sqlite3_errstr(rc));

    if (rc == SQLITE_DONE) {
        goto EARLY_RETURN;
    }

    int col_type = sqlite3_column_type(statement, 0);
    StopIf(col_type != SQLITE_INTEGER, goto EARLY_RETURN, "impossible non-integer returned");

    col_type = sqlite3_column_type(statement, 1);
    StopIf(col_type != SQLITE_FLOAT, goto EARLY_RETURN, "impossible non-float returned");

    static_assert(sizeof(sqlite3_int64) <= sizeof(time_t), "time_t too small");
    sqlite3_int64 timestamp_sql = sqlite3_column_int64(statement, 0);
    time_t timestamp = timestamp_sql;

    double temperature_f = sqlite3_column_double(statement, 1);

    ob->valid_time = timestamp;
    ob->temperature_f = temperature_f;

    return rc;

EARLY_RETURN:

    *ob = (struct ObsTemperature){0};
    return rc;
}

static int
obs_db_query_temperatures_get_hourlies(sqlite3 *db, char const *const site, struct ObsTimeRange tr,
                                       struct ObsTemperature **hourlies, size_t *num_hourlies)
{
    sqlite3_stmt *statement = 0;

    size_t num_rows = obs_db_count_rows_in_range(db, site, tr);
    StopIf(num_rows == SIZE_MAX, goto ERR_RETURN, "error counting number of rows");

    *hourlies = calloc(num_rows, sizeof(**hourlies));
    StopIf(!*hourlies, goto ERR_RETURN, "out of memory");
    struct ObsTemperature *lcl_hourlies = *hourlies;

    char query[256] = {0};
    sprintf(query,
            "SELECT valid_time, t_f "
            "FROM obs "
            "WHERE site='%s' "
            "    AND valid_time >= %ld "
            "    AND valid_time <= %ld "
            "ORDER BY valid_time ASC",
            site, tr.start, tr.end);

    int rc = sqlite3_prepare_v2(db, query, -1, &statement, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error preparing select statement:\n     %s\n     %s",
           query, sqlite3_errstr(rc));

    while (*num_hourlies < num_rows) {
        rc = obs_db_query_temperatures_get_hourlies_step_row(statement,
                                                             &lcl_hourlies[*num_hourlies]);

        StopIf(rc != SQLITE_ROW && rc != SQLITE_DONE, goto ERR_RETURN, "database error: %s",
               sqlite3_errstr(rc));

        if (rc != SQLITE_ROW) {
            break;
        }

        *num_hourlies += 1;
    }

    sqlite3_finalize(statement);

    return 0;

ERR_RETURN:
    free(*hourlies);
    *hourlies = 0;
    *num_hourlies = 0;
    sqlite3_finalize(statement);

    return -1;
}

static double
obs_db_query_temperatures_max_min_in_window(struct ObsTemperature **last_start, size_t *len,
                                            time_t start, time_t end, int max_min_mode)
{
    double max_min_val = NAN;

    struct ObsTemperature *end_ptr = *last_start + *len;

    for (struct ObsTemperature *next = *last_start; next < end_ptr; next++) {
        time_t vt = next->valid_time;
        double val = next->temperature_f;

        // Remember points that are in the past so we can skip them next time.
        if (vt < start) {
            *last_start += 1;
            *len -= 1;
            continue;
        }

        if (vt > end) {
            break;
        }

        if (isnan(max_min_val)) {
            max_min_val = val;
        } else if (max_min_mode == OBS_DB_MAX_MODE && val > max_min_val) {
            max_min_val = val;
        } else if (max_min_mode == OBS_DB_MIN_MODE && val < max_min_val) {
            max_min_val = val;
        }
    }

    return max_min_val;
}

int
obs_db_query_temperatures(sqlite3 *db, int max_min_mode, char const *const site,
                          struct ObsTimeRange tr, unsigned window_end, unsigned window_length,
                          struct ObsTemperature **results, size_t *num_results)
{
    assert(results && !*results && "results is null or points to non-null pointer");
    assert(*num_results == 0 && "*num_results not initalized to zero");

    struct ObsTemperature *hourlies = 0;
    size_t num_hourlies = 0;

    int rc = obs_db_query_temperatures_get_hourlies(db, site, tr, &hourlies, &num_hourlies);
    StopIf(rc < 0, goto ERR_RETURN, "error getting hourly temperatures");

    size_t calc_num_res = obs_db_query_calculate_num_results(tr, 24);
    StopIf(calc_num_res == SIZE_MAX, goto ERR_RETURN, "unable to calculate number of results");

    *results = calloc(calc_num_res, sizeof(**results));
    StopIf(!*results, goto ERR_RETURN, "out of memory");

    struct tm end_prd_tm = *gmtime(&tr.start);
    end_prd_tm.tm_hour = 0;
    end_prd_tm.tm_min = 0;
    end_prd_tm.tm_sec = 0;

    time_t end_prd = timegm(&end_prd_tm);
    while (end_prd < tr.start) {
        end_prd += HOURSEC * 24;
    }

    struct ObsTemperature *lcl_results = *results;
    struct ObsTemperature *last_start = hourlies;
    size_t last_start_size = num_hourlies;
    while (end_prd < tr.end && *num_results < calc_num_res) {
        time_t str_prd = end_prd - HOURSEC * window_length;

        double max_min_t = obs_db_query_temperatures_max_min_in_window(
            &last_start, &last_start_size, str_prd, end_prd, max_min_mode);

        lcl_results[*num_results].valid_time = end_prd;
        lcl_results[*num_results].temperature_f = max_min_t;
        *num_results += 1;

        end_prd += HOURSEC * 24;
    }

    free(hourlies);

    return 0;

ERR_RETURN:

    free(hourlies);

    *num_results = 0;
    free(*results);
    *results = 0;

    return -1;
}

static int
obs_db_query_pecipitation_get_hourlies_step_row(sqlite3_stmt *statement,
                                                struct ObsPrecipitation ob[static 1])
{
    int rc = sqlite3_step(statement);
    StopIf(rc != SQLITE_ROW && rc != SQLITE_DONE, goto EARLY_RETURN, "error executing select: %s\n",
           sqlite3_errstr(rc));

    if (rc == SQLITE_DONE) {
        goto EARLY_RETURN;
    }

    int col_type = sqlite3_column_type(statement, 0);
    StopIf(col_type != SQLITE_INTEGER, goto EARLY_RETURN, "impossible non-integer returned");

    col_type = sqlite3_column_type(statement, 1);
    StopIf(col_type != SQLITE_FLOAT, goto EARLY_RETURN, "impossible non-float returned");

    static_assert(sizeof(sqlite3_int64) <= sizeof(time_t), "time_t too small");
    sqlite3_int64 timestamp_sql = sqlite3_column_int64(statement, 0);
    time_t timestamp = timestamp_sql;

    double precip = sqlite3_column_double(statement, 1);

    ob->valid_time = timestamp;
    ob->precip_in = precip;

    return rc;

EARLY_RETURN:

    *ob = (struct ObsPrecipitation){0};
    return rc;
}

static int
obs_db_query_precipitation_get_hourlies(sqlite3 *db, char const *const site, struct ObsTimeRange tr,
                                        struct ObsPrecipitation **hourlies, size_t *num_hourlies)
{
    sqlite3_stmt *statement = 0;

    size_t num_rows = obs_db_count_rows_in_range(db, site, tr);
    StopIf(num_rows == SIZE_MAX, goto ERR_RETURN, "error counting number of rows");

    *hourlies = calloc(num_rows, sizeof(**hourlies));
    StopIf(!*hourlies, goto ERR_RETURN, "out of memory");
    struct ObsPrecipitation *lcl_hourlies = *hourlies;

    char query[256] = {0};
    sprintf(query,
            "SELECT valid_time, precip_in_1hr "
            "FROM obs "
            "WHERE site='%s' "
            "    AND valid_time >= %ld "
            "    AND valid_time <= %ld "
            "ORDER BY valid_time ASC",
            site, tr.start, tr.end);

    int rc = sqlite3_prepare_v2(db, query, -1, &statement, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error preparing select statement:\n     %s\n     %s",
           query, sqlite3_errstr(rc));

    while (*num_hourlies < num_rows) {
        rc = obs_db_query_pecipitation_get_hourlies_step_row(statement,
                                                             &lcl_hourlies[*num_hourlies]);

        StopIf(rc != SQLITE_ROW && rc != SQLITE_DONE, goto ERR_RETURN, "database error: %s",
               sqlite3_errstr(rc));

        if (rc != SQLITE_ROW) {
            break;
        }

        *num_hourlies += 1;
    }

    sqlite3_finalize(statement);

    return 0;

ERR_RETURN:
    free(*hourlies);
    *hourlies = 0;
    *num_hourlies = 0;
    sqlite3_finalize(statement);

    return -1;
}

static double
obs_db_query_precipitation_accumulation_in_window(struct ObsPrecipitation **last_start, size_t *len,
                                                  time_t start, time_t end)
{
    double sum_val = 0.0;
    int last_hour = -1;
    double last_hour_val = 0.0;
    bool trace_flag = false;

    struct ObsPrecipitation *end_ptr = *last_start + *len;

    for (struct ObsPrecipitation *next = *last_start; next < end_ptr; next++) {
        time_t vt = next->valid_time;
        double val = next->precip_in;

        // Remember points that are in the past so we can skip them next time.
        if (vt < start) {
            *last_start += 1;
            *len -= 1;
            continue;
        }

        if (vt > end) {
            break;
        }

        if (val < 0.01 && val > 0.0) {
            trace_flag = true;
        } else {
            struct tm vt_tm = *gmtime(&vt);
            int hour = vt_tm.tm_hour;
            if (hour != last_hour) {
                sum_val += last_hour_val;
            }
            last_hour = hour;
            last_hour_val = val;
        }
    }

    sum_val += last_hour_val;

    if (trace_flag && sum_val < 0.005) {
        return 0.001;
    }

    return sum_val;
}

int
obs_db_query_precipitation(sqlite3 *db, char const *const site, struct ObsTimeRange tr,
                           unsigned window_length, unsigned window_increment,
                           struct ObsPrecipitation **results, size_t *num_results)
{
    assert(results && !*results && "results is null or points to non-null pointer");
    assert(*num_results == 0 && "*num_results not initalized to zero");

    struct ObsPrecipitation *hourlies = 0;
    size_t num_hourlies = 0;

    int rc = obs_db_query_precipitation_get_hourlies(db, site, tr, &hourlies, &num_hourlies);
    StopIf(rc < 0, goto ERR_RETURN, "error getting hourly precipitation");

    size_t calc_num_res = obs_db_query_calculate_num_results(tr, window_increment);
    StopIf(calc_num_res == SIZE_MAX, goto ERR_RETURN, "unable to calculate number of results");

    *results = calloc(calc_num_res, sizeof(**results));
    StopIf(!*results, goto ERR_RETURN, "out of memory");

    struct tm end_prd_tm = *gmtime(&tr.start);
    end_prd_tm.tm_hour = 0;
    end_prd_tm.tm_min = 0;
    end_prd_tm.tm_sec = 0;

    time_t end_prd = timegm(&end_prd_tm);
    while (end_prd < tr.start) {
        end_prd += HOURSEC * window_increment;
    }

    struct ObsPrecipitation *lcl_results = *results;
    struct ObsPrecipitation *last_start = hourlies;
    size_t last_start_size = num_hourlies;
    while (end_prd < tr.end && *num_results < calc_num_res) {
        time_t str_prd = end_prd - HOURSEC * window_length;
        double pcp_accum = obs_db_query_precipitation_accumulation_in_window(
            &last_start, &last_start_size, str_prd, end_prd);

        lcl_results[*num_results].valid_time = end_prd;
        lcl_results[*num_results].precip_in = pcp_accum;
        *num_results += 1;

        end_prd += HOURSEC * window_increment;
    }

    free(hourlies);

    return 0;

ERR_RETURN:

    free(hourlies);

    *num_results = 0;
    free(*results);
    *results = 0;

    return -1;
}

int
obs_db_start_transaction(sqlite3 *db)
{
    char *sqlite_error_message = 0;
    sqlite3_exec(db, "BEGIN TRANSACTION;", 0, 0, &sqlite_error_message);
    StopIf(sqlite_error_message, goto ERR_RETURN, "error starting transaction: %s",
           sqlite_error_message);

    return 0;

ERR_RETURN:

    sqlite3_free(sqlite_error_message);
    return -1;
}

int
obs_db_finish_transaction(sqlite3 *db, int action)
{
    char *sqlite_error_message = 0;
    char *sql = 0;

    if (action == OBS_DB_TRANSACTION_ROLLBACK) {
        sql = "ROLLBACK TRANSACTION;";
    } else if (action == OBS_DB_TRANSACTION_COMMIT) {
        sql = "COMMIT TRANSACTION;";
    } else {
        StopIf(true, goto ERR_RETURN, "inavlid action in obs_db_finish_transaction");
    }

    sqlite3_exec(db, sql, 0, 0, &sqlite_error_message);
    StopIf(sqlite_error_message, goto ERR_RETURN, "error finishing transaction: %s",
           sqlite_error_message);

    return 0;

ERR_RETURN:

    sqlite3_free(sqlite_error_message);
    return -1;
}

sqlite3_stmt *
obs_db_create_insert_statement(sqlite3 *db)
{
    sqlite3_stmt *insert_stmt = 0;
    char const *const sql = "INSERT OR REPLACE INTO obs ( \n"
                            "  valid_time,                \n"
                            "  site,                      \n"
                            "  t_f,                       \n"
                            "  precip_in_1hr)             \n"
                            "VALUES (?,?,?,?);            \n";

    int rc = sqlite3_prepare_v2(db, sql, -1, &insert_stmt, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error creating sqlite3_stmt: %s", sqlite3_errstr(rc));

    return insert_stmt;

ERR_RETURN:

    sqlite3_finalize(insert_stmt);
    return 0;
}

void
obs_db_finalize_insert_statement(sqlite3_stmt *stmt)
{
    if (stmt) {
        sqlite3_finalize(stmt);
    }

    return;
}

int
obs_db_insert(sqlite3_stmt *insert_stmt, time_t valid_time, char const *const site_id,
              double temperature_f, double precip_inches)
{

    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);

    int rc = sqlite3_bind_int64(insert_stmt, 1, valid_time);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error binding valid_time: %s", sqlite3_errstr(rc));

    rc = sqlite3_bind_text(insert_stmt, 2, site_id, -1, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error binding site: %s", sqlite3_errstr(rc));

    rc = sqlite3_bind_double(insert_stmt, 3, temperature_f);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error binding t_f: %s", sqlite3_errstr(rc));

    rc = sqlite3_bind_double(insert_stmt, 4, precip_inches);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error binding p_in: %s", sqlite3_errstr(rc));

    rc = sqlite3_step(insert_stmt);
    StopIf(rc != SQLITE_OK && rc != SQLITE_DONE, goto ERR_RETURN,
           "error stepping sqlite statement: %s", sqlite3_errstr(rc));

    return 0;

ERR_RETURN:
    return -1;
}
