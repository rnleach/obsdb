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
    /** Local, on disk storage. */
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
    struct ObsStore *new = calloc(1, sizeof(*new));
    StopIf(!new, return 0, "Memory allocation error.");

    sqlite3 *db = obs_db_open_create();
    StopIf(!db, goto ERR_RETURN, "unable to connect to sqlite");

    struct ObsStore new_static = {
        .synoptic_labs_api_key = synoptic_labs_api_key, .db = db, .curl = 0};

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

    struct ObsStore *ptr = *store;

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
 * \param store - same as \ref obs_query_max_t()
 * \param site - same as \ref obs_query_max_t()
 * \param time_range - same as \ref obs_query_max_t()
 * \param window_end - same as \ref obs_query_max_t()
 * \param window_length - same as \ref obs_query_max_t()
 * \param results - same as \ref obs_query_max_t()
 * \param num_results - same as \ref obs_query_max_t()
 * \param max_min_mode is either \ref OBS_DB_MAX_MODE or \ref OBS_DB_MIN_MODE.
 *
 * All parameters other than \a max_min_mode are as in \ref obs_query_max_t() and
 * \ref obs_query_min_t().
 */
static int
obs_store_query_t(struct ObsStore *store, char const *const site, struct ObsTimeRange tr,
                  unsigned window_end, unsigned window_length, struct ObsTemperature **results,
                  size_t *num_results, int max_min_mode)
{
    assert(store);
    assert(site);
    assert(tr.start < tr.end && "backwards time range");
    assert(window_end <= 24 && "there is only 24 hours in a day");
    assert(results && !*results && num_results && !*num_results);
    assert(max_min_mode == OBS_DB_MAX_MODE || max_min_mode == OBS_DB_MIN_MODE);

    char site_buf[32] = {0};
    obs_util_strcpy_to_lowercase(sizeof(site_buf), site_buf, site);

    // Expand the time range to get enough data for ALL windows ending in the provided time range.
    struct ObsTimeRange need_hourlies_tr = tr;
    need_hourlies_tr.start -= HOURSEC * window_length;

    struct ObsTimeRange *missing_ranges = 0;
    size_t num_missing_ranges = 0;

    int have_data = obs_db_have_inventory(store->db, site_buf, need_hourlies_tr, &missing_ranges,
                                          &num_missing_ranges);

    StopIf(have_data < 0, return -1, "temperature query aborted, database error.");

    int rc = 0;
    if (!have_data) {
        for (size_t i = 0; i < num_missing_ranges; i++) {

            rc = obs_download(store->db, &store->curl, store->synoptic_labs_api_key, site_buf,
                              missing_ranges[i]);

            StopIf(rc < 0, goto ERR_RETURN, "Error downloading data.");
        }
    }

    // Just take whatever data is available from the database now that we've tried to update it.
    rc = obs_db_query_temperatures(store->db, max_min_mode, site_buf, tr, window_end, window_length,
                                   results, num_results);
    StopIf(rc < 0, goto ERR_RETURN, "Error fetching data from local store.");

    free(missing_ranges);
    return rc;

ERR_RETURN:

    // Ensure these invariants are still in place.
    assert(num_results && !*num_results && results && !*results);
    free(missing_ranges);
    return rc;
}

int
obs_query_max_t(struct ObsStore *store, char const *const site, struct ObsTimeRange tr,
                unsigned window_end, unsigned window_length, struct ObsTemperature **results,
                size_t *num_results)
{
    // These conditions are specified in the documentation.
    assert(store);
    assert(site);
    assert(tr.start < tr.end && "backwards time range");
    assert(window_end <= 24 && "there is only 24 hours in a day");
    assert(num_results && !*num_results && results && !*results);

    return obs_store_query_t(store, site, tr, window_end, window_length, results, num_results,
                             OBS_DB_MAX_MODE);
}

int
obs_query_min_t(struct ObsStore *store, char const *const site, struct ObsTimeRange tr,
                unsigned window_end, unsigned window_length, struct ObsTemperature **results,
                size_t *num_results)
{
    // These conditions are specified in the documentation.
    assert(store);
    assert(site);
    assert(tr.start < tr.end && "backwards time range");
    assert(window_end <= 24 && "there is only 24 hours in a day");
    assert(num_results && !*num_results && results && !*results);

    return obs_store_query_t(store, site, tr, window_end, window_length, results, num_results,
                             OBS_DB_MIN_MODE);
}

int
obs_query_precipitation(struct ObsStore *store, char const *const site, struct ObsTimeRange tr,
                        unsigned window_length, unsigned window_increment, unsigned window_offset,
                        struct ObsPrecipitation **results, size_t *num_results)
{
    // These conditions are specified in the documentation.
    assert(store);
    assert(site);
    assert(tr.start < tr.end && "backwards time range");
    assert(window_offset <= 24 && "there is only 24 hours in a day");
    assert(num_results && !*num_results && results && !*results);

    char site_buf[32] = {0};
    obs_util_strcpy_to_lowercase(sizeof(site_buf), site_buf, site);

    // Expand the time range to get enough data for ALL windows ending in the provided time range.
    struct ObsTimeRange need_hourlies_tr = tr;
    need_hourlies_tr.start -= HOURSEC * window_length;

    struct ObsTimeRange *missing_ranges = 0;
    size_t num_missing_ranges = 0;

    int have_data = obs_db_have_inventory(store->db, site_buf, need_hourlies_tr, &missing_ranges,
                                          &num_missing_ranges);

    StopIf(have_data < 0, return -1, "precipitation query aborted, database error.");

    int rc = 0;
    if (!have_data) {
        for (size_t i = 0; i < num_missing_ranges; i++) {
            rc = obs_download(store->db, &store->curl, store->synoptic_labs_api_key, site_buf,
                              missing_ranges[i]);

            StopIf(rc < 0, goto ERR_RETURN, "Error downloading data.");
        }
    }

    // Just take whatever data is available from the database now that we have tried to update it.
    rc = obs_db_query_precipitation(store->db, site_buf, tr, window_length, window_increment,
                                    window_offset, results, num_results);
    StopIf(rc < 0, goto ERR_RETURN, "Error fetching data from local store.");

    free(missing_ranges);

    return rc;

ERR_RETURN:

    // Ensure these invariants are still in place.
    assert(num_results && !*num_results && results && !*results);
    free(missing_ranges);
    return rc;
}
