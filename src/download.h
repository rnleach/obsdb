/** \file download.h
 * \brief The interface to the SynopticLabs API for downloading observations.
 */
#pragma once

#include "obs.h"

#include <curl/curl.h>
#include <sqlite3.h>

/** Download data and save it in the local store.
 *
 * \param local_store is a handle to the local store. Downloaded observations will be added here for
 * future queries.
 * \param curl is a pointer to a \c CURL handle. If it points to a \c NULL handle, then it will be
 * initialized. The \c CURL instance is used for doing the actual downloading.
 * \param synoptic_labs_api_key is a \c NULL terminated string with the SynopticLabs API key.
 * \param site_id is a \c NULL terminated string, all lowercase, with the SynopticLabs site
 * identifier.
 * \param time_range is the time range to request data for.
 *
 * \returns 0 on success and -1 on failure.
 */
int obs_download(sqlite3 *local_store, CURL **curl, char const *const synoptic_labs_api_key,
                 char const *site_id, struct ObsTimeRange time_range);
