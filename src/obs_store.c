/** \file obs_store.c
 *
 * \brief Implementation of the public API.
 */

#include "download.h"
#include "obs.h"
#include "obs_db.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <sqlite3.h>

/** Abstraction of a data source.
 *
 * Abstracts away whether data is retrieved from a local database or retrieved from the web.
 * Data retrieved from the web will be stored in the local archive so future requests can be
 * fulfilled locally instead of via a web request.
 */
struct ObsStore {
    /** Local, on disk storage.*/
    sqlite3 *db;

    /** Handle to cURL object in case a web request is needed. */
    CURL *curl;

    /** API Key for SynopticLabs API.
     *
     * This is an alias, so it must not be freed.
     */
    char const *const synoptic_labs_api_key;
};

struct ObsStore *
obs_connect(char const *const synoptic_labs_api_key)
{
    // using calloc forces both members to null.
    struct ObsStore *new = calloc(1, sizeof(struct ObsStore));
    StopIf(!new, return 0, "Memory allocation error.");

    sqlite3 *db = obs_db_open_create();
    StopIf(!db, goto ERR_RETURN, "unable to connect to sqlite");

    ObsStore new_static = {.synoptic_labs_api_key = synoptic_labs_api_key, .db = db, .curl = 0};
    memcpy(new, &new_static, sizeof(*new));

    return new;

ERR_RETURN:

    free(new);
    return 0;
}

void
obs_close(struct ObsStore **store)
{
    StopIf(!store || !(*store), return, "Warning NULL passed for obs_close.");

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

/** Internal implementation of obs_query_max_t() and obs_query_min_t().
 *
 * \param store - same as obs_query_max_t()
 * \param site - same as obs_query_max_t()
 * \param start_time - same as obs_query_max_t()
 * \param end_time - same as obs_query_max_t()
 * \param window_length - same as obs_query_max_t()
 * \param results - same as obs_query_max_t()
 * \param num_results - same as obs_query_max_t()
 * \param max_min_mode is either OBS_DB_MAX_MODE or OBS_DB_MIN_MODE.
 *
 * All parameters other than \a max_min_mode are as in obs_query_max_t() and obs_query_min_t().
 */
static int
obs_query_t(struct ObsStore *store, char const *const site, time_t start_time, time_t end_time,
            unsigned window_length, struct TemperatureOb **results, size_t *num_results,
            int max_min_mode)
{
    assert(max_min_mode == OBS_DB_MAX_MODE || max_min_mode == OBS_DB_MIN_MODE);

    char site_buf[32] = {0};
    obs_util_strcpy_to_lowercase(sizeof(site_buf), site_buf, site);

    int have_data = obs_db_have_inventory(store->db, "temperature", site_buf, start_time, end_time);

    char *msg = 0;
    if (max_min_mode == OBS_DB_MAX_MODE) {
        msg = "maximum";
    } else if (max_min_mode == OBS_DB_MIN_MODE) {
        msg = "minimum";
    }

    StopIf(have_data < 0, return -1, "%s temperature query aborted, database error.", msg);

    int rc = 0;
    if (!have_data) {
        rc = obs_download(store->db, &store->curl, store->synoptic_labs_api_key, site_buf,
                          start_time, end_time);

        StopIf(rc < 0, goto ERR_RETURN, "Error downloading data.");
    }

    // Just take whatever data is available from the database now that we've tried to update it.
    rc = obs_db_query_temperatures(store->db, max_min_mode, site_buf, start_time, end_time,
                                   window_length, results, num_results);
    StopIf(rc < 0, goto ERR_RETURN, "Error fetching data from local store.");

    return rc;

ERR_RETURN:

    // Ensure these invariants are still in place.
    assert(*num_results == 0 && results && !*results);
    return rc;
}

int
obs_query_max_t(struct ObsStore *store, char const *const site, time_t start_time, time_t end_time,
                unsigned window_length, struct TemperatureOb **results, size_t *num_results)
{
    // These conditions are specified in the documentation.
    assert(*num_results == 0 && results && !*results);

    return obs_query_t(store, site, start_time, end_time, window_length, results, num_results,
                       OBS_DB_MAX_MODE);
}

int
obs_query_min_t(struct ObsStore *store, char const *const site, time_t start_time, time_t end_time,
                unsigned window_length, struct TemperatureOb **results, size_t *num_results)
{
    // These conditions are specified in the documentation.
    assert(*num_results == 0 && results && !*results);

    return obs_query_t(store, site, start_time, end_time, window_length, results, num_results,
                       OBS_DB_MIN_MODE);
}

int
obs_query_precipitation(struct ObsStore *store, char const *const site, time_t start_time,
                        time_t end_time, unsigned window_length, unsigned window_increment,
                        struct PrecipitationOb **results, size_t *num_results)
{
    // These conditions are specified in the documentation.
    assert(*num_results == 0 && results && !*results);

    char site_buf[32] = {0};
    obs_util_strcpy_to_lowercase(sizeof(site_buf), site_buf, site);

    int have_data =
        obs_db_have_inventory(store->db, "precipitation", site_buf, start_time, end_time);

    StopIf(have_data < 0, return -1, "precipitation query aborted, database error.");

    int rc = 0;
    if (!have_data) {
        rc = obs_download(store->db, &store->curl, store->synoptic_labs_api_key, site_buf,
                          start_time, end_time);

        StopIf(rc < 0, goto ERR_RETURN, "Error downloading data.");
    }

    // Just take whatever data is available from the database now that we have tried to update it.
    rc = obs_db_query_precipitation(store->db, site_buf, start_time, end_time, window_length,
                                    window_increment, results, num_results);
    StopIf(rc < 0, goto ERR_RETURN, "Error fetching data from local store.");

    return rc;

ERR_RETURN:

    // Ensure these invariants are still in place.
    assert(*num_results == 0 && results && !*results);
    return rc;
}
