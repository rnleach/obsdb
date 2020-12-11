/** \file obs_store.c
 *
 * \brief Implementation of the public API.
 */

#include "obs.h"
#include "obs_db.h"
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <sqlite3.h>

struct ObsStore {
    sqlite3 *db;
    CURL *curl;
};

struct ObsStore *
obs_connect(void)
{
    // using calloc forces both members to null.
    struct ObsStore *new = calloc(1, sizeof(struct ObsStore));
    StopIf(!new, return 0, "Memory allocation error.");

    sqlite3 *db = obs_db_open_create();
    StopIf(!db, goto ERR_RETURN, "unable to connect to sqlite");

    new->db = db;

    return new;

ERR_RETURN:

    free(new);
    return 0;
}

void
obs_close(struct ObsStore **store)
{
    assert(store && *store);

    ObsStore *ptr = *store;

    int result = obs_db_close(ptr->db);
    if (result != SQLITE_OK) {
        fprintf(stderr, "ERROR closing ObsStore.\n");
    }

    // Clean up curl if necessary.
    if (ptr->curl) {
        curl_easy_cleanup(ptr->curl);
        curl_global_cleanup();
    }

    // Nullify the pointer.
    *store = 0;

    return;
}

int
obs_query_max_t(struct ObsStore *store, char const *const site, time_t start_time, time_t end_time,
                unsigned window_length, struct TemperatureOb **results, size_t *num_results)
{
    int have_data =
        obs_db_have_inventory(store->db, "temperature", site, start_time, end_time, 100);
    StopIf(have_data < 0, return -1, "maximum temperature query aborted, database error.");

    if (!have_data) {
        /* TODO ask it download more data. */
        // If it doesn't have it, ask the download module to download it.
        assert(0);
    }

    // Just take whatever data is available from the database.
    return obs_db_query_temperatures(store->db, OBS_DB_MAX_MODE, site, start_time, end_time,
                                  window_length, results, num_results);
}

int
obs_query_min_t(struct ObsStore *store, char const *const site, time_t start_time, time_t end_time,
                unsigned window_length, struct TemperatureOb **results, size_t *num_results)
{
    // Ask the database if it has all the data
    // If it doesn't have it, ask the download module to download it.
    // Then get the data from the database - just take whatever you can get.

    /* TODO */
    assert(0);
    return -1;
}

int
obs_query_precipitation(struct ObsStore *store, char const *const site, time_t start_time,
                        time_t end_time, unsigned window_length, unsigned window_increment,
                        struct PrecipitationOb **results, size_t *num_results)
{
    // Ask the database if it has all the data
    // If it doesn't have it, ask the download module to download it.
    // Then get the data from the database - just take whatever you can get.

    /* TODO*/
    assert(0);
    return -1;
}
