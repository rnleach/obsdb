/** \file obs_db.c
 *
 * \brief Internal implementation of the local portion of the ObsStore
 */
#include "obs.h"
#include "obs_db.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
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

    char *sql = "CREATE TABLE IF NOT EXISTS obs (                                 \n"
                "  site       TEXT    NOT NULL, -- Synoptic Labs API site id      \n"
                "  valid_time INTEGER NOT NULL, -- unix time stamp of valid time. \n"
                "  t_f        REAL,             -- temperature in Fahrenheit      \n"
                "  precip_in  REAL,             -- precipitation in inches        \n"
                "  PRIMARY KEY (site, valid_time));                               \n";

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

int
obs_db_have_inventory(sqlite3 *db, char const *const element, char const *const site,
                      time_t start_time, time_t end_time)
{
    assert(strcmp("temperature", element) == 0 || strcmp("precipitation", element) == 0);
    assert(start_time < end_time);

    char *qry_elmt = 0;
    if (strcmp("temperature", element) == 0) {
        qry_elmt = "t_f";
    } else if (strcmp("precipitation", element) == 0) {
        qry_elmt = "precip_in";
    } else {
        assert(false);
    }

    char query[256] = {0};
    sprintf(query,
            "SELECT COUNT(%s) FROM obs "
            "WHERE site='%s' AND valid_time >= %ld AND valid_time <= %ld",
            qry_elmt, site, start_time, end_time);

    sqlite3_stmt *statement = 0;
    int rc = sqlite3_prepare_v2(db, query, -1, &statement, 0);
    StopIf(rc != SQLITE_OK, goto ERR_RETURN, "error preparing select statement:\n     %s\n     %s",
           query, sqlite3_errstr(rc));

    rc = sqlite3_step(statement);
    StopIf(rc != SQLITE_ROW, goto ERR_RETURN, "error executing select sql:\n     %s\n     %s",
           query, sqlite3_errstr(rc));

    int col_type = sqlite3_column_type(statement, 0);
    StopIf(col_type != SQLITE_INTEGER, goto ERR_RETURN, "impossible non-integer returned");

    static_assert(sizeof(sqlite3_int64) <= sizeof(long), "long too small");
    sqlite3_int64 db_count64 = sqlite3_column_int64(statement, 0);
    long db_count = db_count64;

    sqlite3_finalize(statement);

    static_assert(sizeof(time_t) <= sizeof(long), "time_t too large");
    // Assumes time_t is in units of seconds. Guaranteed by POSIX?
    // This assumes 1 observation per hour, +1 to be inclusive of start and end times.
    long calc_count = (end_time - start_time + 1) / 60;

    if (calc_count > db_count) {
        return 0;
    } else {
        return 1;
    }

ERR_RETURN:

    sqlite3_finalize(statement);
    return -1;
}

int
obs_db_query_temperatures(sqlite3 *db, int max_min_mode, char const *const site, time_t start_time,
                          time_t end_time, unsigned window_length, struct TemperatureOb **results,
                          size_t *num_results)
{
    /* TODO */
    assert(false);

    return -1;
}

int
obs_db_query_precipitation(sqlite3 *db, char const *const site, time_t start_time, time_t end_time,
                           unsigned window_length, unsigned window_increment,
                           struct PrecipitationOb **results, size_t *num_results)
{
    /* TODO */
    assert(false);

    return -1;
}
