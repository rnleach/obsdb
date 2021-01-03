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
 * \returns \c 0 on error.
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
 * \param site is the site in question, it must be in all lowercase.
 * \param time_range the time range the query will cover.
 * \param missing_times If this is not \c 0, then any time ranges with missing data will be placed
 * in an allocated array here. If this is not \c 0, and there are no missing time ranges, the
 * pointed to pointer will be \c 0. \ref *missing_times must be \c 0 upon entry.
 * \param num_missing_times must not be \c 0 if \ref missing_times is not \c 0, otherwise it is
 * ignored. The number of values returned in \ref missing_times will be stored here. If there are no
 * missing times, this will be set to zero.
 *
 * \returns -1 if there is an error, 0 if not enough data was available, and 1 if enough data is
 * available.
 */
int obs_db_have_inventory(sqlite3 *db, char const *const site, struct ObsTimeRange time_range,
                          struct ObsTimeRange **missing_times, size_t *num_missing_times);

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
 * \param time_range the time range the query will cover.
 * \param window_end is the hour of the day (UTC) that the window should end.
 * \param window_length is the number of hours long the window is for each valid time.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free().
 * \param num_results will be the number of \ref TemperatureOb objects stored in \ref results.
 *
 * \returns 0 on success, or a negative number upon failure. If there is an error \ref results will
 * be \c NULL and \ref num_results will be set to zero.
 *
 */
int obs_db_query_temperatures(sqlite3 *db, int max_min_mode, char const *const site,
                              struct ObsTimeRange time_range, unsigned window_end,
                              unsigned window_length, struct TemperatureOb **results,
                              size_t *num_results);

/** Execute a query for temperatures.
 *
 * \param db the database handle to query.
 * \param site is the site in question, it must be in all lowercase.
 * \param time_range the time range the query will cover.
 * \param window_length - the window length in hours.
 * \param window_increment - the time in hours between when windows start.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL (or \c 0) when passed in to ensure there is no memory
 * leak.
 * \param num_results will be the number of PrecipitationOb objects stored in \ref results. This
 * must be 0 when passed in so it is consistent with the length of \ref results.
 *
 *
 * \returns 0 on success, or a negative number upon failure. If there is an error \ref results will
 * be \c NULL and \ref num_results will be set to zero, which should be the same as when they were
 * passed in as arguments.
 *
 */
int obs_db_query_precipitation(sqlite3 *db, char const *const site, struct ObsTimeRange time_range,
                               unsigned window_length, unsigned window_increment,
                               struct PrecipitationOb **results, size_t *num_results);

/** Start a transaction on the local store.
 *
 * \param db the database handle.
 *
 * \returns 0 on success or a negative number on failure.
 */
int obs_db_start_transaction(sqlite3 *db);

/** Indicate that transaction should be committed. */
#define OBS_DB_TRANSACTION_COMMIT 0

/** Indicate that transaction should be rolled back. */
#define OBS_DB_TRANSACTION_ROLLBACK 1

/** Finish a transaction on the local store.
 *
 * \param db the database handle.
 * \param action whether to commit or rollback, should be \ref OBS_DB_TRANSACTION_ROLLBACK or
 * \ref OBS_DB_TRANSACTION_COMMIT.
 *
 * \returns 0 on success or a negative number on failure.
 */
int obs_db_finish_transaction(sqlite3 *db, int action);

/** Create an insert statement for the local store.
 *
 * \param db the database to make the prepared statement on.
 *
 * \returns the statement handle or NULL on failure.
 */
sqlite3_stmt *obs_db_create_insert_statement(sqlite3 *db);

/** Finalize and clean up the insert statement.
 *
 * \param stmt is the statement to clean up.
 *
 */
void obs_db_finalize_insert_statement(sqlite3_stmt *stmt);

/** Insert some values into the local store.
 *
 * \param insert_stmt is a statement returned by \ref obs_db_create_insert_statement()
 * \param valid_time is the valid time of the observation.
 * \param site_id is the SynopticLabs (mesowest) site id.
 * \param temperature_f is the temperature in Fahrenheit.
 * \param precip_inches is the 1-hour precipitation in inches.
 *
 * \returns 0 on success or a negative number on failure.
 */
int obs_db_insert(sqlite3_stmt *insert_stmt, time_t valid_time, char const *const site_id,
                  double temperature_f, double precip_inches);
