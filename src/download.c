/** \file download.c
 * \brief Implementation of the download module.
 */
#include "download.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>

#include <curl/curl.h>
#include <sqlite3.h>

int 
obs_download(sqlite3 *local_store, CURL **curl, char const *const synoptic_labs_api_key,
                 char const *site_id, time_t start, time_t end)
{
    /* TODO unimplemented. */
    assert(false);

    return -1;
}
