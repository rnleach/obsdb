#pragma once
/** \file obs_db.h
 *
 * \brief Local database store.
 *
 * This uses sqlite3 behind the scenes and stores data in a database on the local file system.
 */
#include <obs.h>

#include <time.h>

#include <sqlite3.h>

/** Connect to the local database storage.
 *
 * If the database does not exist, it will create the full path to the file and the file, then
 * open the database connection.
 *
 * \returns \c NULL on error.
 */
sqlite3 *obs_db_open_create(void);

/** Close down the database.
 *
 * \returns 0 on success, less than zero otherwise.
 */
int obs_db_close(sqlite3 *db);

/** Query the database to see if a request can be fulfilled.
 *
 * \param db the database handle to query.
 * \param element the weather element we want. Currently only "temperature" and "precipitation" are
 * accepted.
 * \param site is the site in question, it must be in all lowercase.
 * \param start_time is the start time of the query.
 * \param end_time is the end time of the query.
 *
 * \returns -1 if there is a database error, 0 if not enough data was available, and 1 if enough
 * data is available.
 */
int obs_db_have_inventory(sqlite3 *db, char const *const element, char const *const site,
                          time_t start_time, time_t end_time);

/** Get maximum temperatures.
 *
 * This is meant to be the second argument to obs_db_query_temperatures() to indicate that you want
 * the maximum temperature during that period.
 */
#define OBS_DB_MAX_MODE 1

/** Get minimum temperatures.
 *
 * This is meant to be the second argument to obs_db_query_temperatures() to indicate that you want
 * the minimum temperature during that period.
 */
#define OBS_DB_MIN_MODE 2

/** Execute a query for temperatures.
 *
 * \param db the database handle to query.
 * \param max_min_mode is an integer to select whether to query maximum or minimum temperature. See
 * macros OBS_DB_MAX_MODE and OBS_DB_MIN_MODE.
 * \param site is the site in question, it must be in all lowercase.
 * \param start_time is the start time of the query.
 * \param end_time is the end time of the query.
 * \param window_length is the number of hours long the window is for each valid time.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free().
 * \param num_results will be the number of \c TemperatureOb objects stored in \c results.
 *
 * \returns 0 on success, or a negative number upon failure. If there is an error *results will be
 * \c NULL and *num_results will be set to zero.
 *
 * The returned values are the maximum temperature within a window of \c window_length. The first
 * window starts at \c start_time, and each subsequent window starts 24 hours later.
 */
int obs_db_query_temperatures(sqlite3 *db, int max_min_mode, char const *const site,
                              time_t start_time, time_t end_time, unsigned window_length,
                              struct TemperatureOb **results, size_t *num_results);

/** Execute a query for temperatures.
 *
 * \param db the database handle to query.
 * \param site is the site in question, it must be in all lowercase.
 * \param start_time is the start time of the query.
 * \param end_time is the end time of the query.
 * \param window_length - the window length in hours.
 * \param window_increment - the time in hours between when windows start.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak. It's
 * the user's responsibility to manage this memory, this function will not free it for you.
 * \param num_results will be the number of PrecipitationOb objects stored in \a results. This must
 * be 0 when passed in so it is consistent with the length of \a results.
 *
 *
 * \returns 0 on success, or a negative number upon failure. If there is an error *results will be
 * \c NULL and *num_results will be set to zero, which should be the same as when they were passed
 * in as arguments.
 *
 */
int obs_db_query_precipitation(sqlite3 *db, char const *const site, time_t start_time,
                               time_t end_time, unsigned window_length, unsigned window_increment,
                               struct PrecipitationOb **results, size_t *num_results);
