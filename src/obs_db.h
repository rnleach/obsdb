#pragma once
/** \file obs_db.h
 *
 * \brief Local database store.
 *
 * This uses sqlite3 behind the scenes and stores data in a database on the local file system.
 */
#include <time.h>

#include <sqlite3.h>

/** Connect to the local database storage.
 *
 * If the database does not exist, it will create the full path to the file and the file, then
 * open the database connection.
 *
 * \returns \c NULL on error.
 */
// TODO make implementation stub
sqlite3 *obs_db_open_create(void);

/** Close down the database.
 *
 * \returns 0 on success, less than zero otherwise.
 */
// TODO make implementation stub
int obs_db_close(sqlite3 *db);

/** Query the database to see if a request can be fulfilled.
 *
 * \param db the database handle to query.
 * \param element the weather element we want. Currently only "temperature" and "precipitation" are
 * accepted.
 * \param site is the site in question, it must be in all lowercase.
 * \param start_time is the start time of the query.
 * \param end_time is the end time of the query.
 * \param rate is a number between 0 and 100. If the percentage of available data is less than this
 * level, it will try to download more data.
 *
 * \returns -1 if there is a database error, 0 if not enough data was available, and 1 if enough
 * data is available.
 */
// TODO make implementation stub
int obs_db_have_inventory(sqlite3 *db, char const *const element, char const *const site,
                          time_t start_time, time_t end_time, unsigned rate);

/** Get maximum temperatures.
 *
 * This is meant to be the second argument to obs_query_temperatures() to indicate that you want
 * the maximum temperature during that period.
 */
#define OBS_DB_MAX_MODE 1

/** Get minimum temperatures.
 *
 * This is meant to be the second argument to obs_query_temperatures() to indicate that you want
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
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the maximum temperature within a window of \c window_length. The first
 * window starts at \c start_time, and each subsequent window starts 24 hours later.
 */
// TODO make implementation stub
int obs_db_query_temperatures(sqlite3 *db, int max_min_mode, char const *const site,
                              time_t start_time, time_t end_time, unsigned window_length,
                              struct TemperatureOb **results, size_t *num_results);
