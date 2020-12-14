#pragma once
/** \file obs.h
 *
 * API for the obsdb or weather observation archive.
 *
 * This library serves as a library and archive for weather observations. It will download data if
 * necessary and back fill the archive.
 */

#include <time.h>

/** A temperature observation. */
struct TemperatureOb {
    /** Valid time of the observation.
     *
     * If the observation is valid for a specific time, the the valid time will have that time. If
     * it is valid for a period, for instance if it is a daily maximum or minumum temperature, the
     * valid time is for the time at the beginning of the valid period. The length of the valid
     * period will have to be deduced from the function call that created this object.
     */
    time_t valid_time;

    /** The temperature in Fahrenheit. */
    double temperature_f;
};

/** A precipitation observation. */
struct PrecipitationOb {
    /** The time of the END of the accumulation period.
     *
     * The length of the period depends on the arguments to the function that generated this
     * observation report.
     */
    time_t valid_time;

    /** The precipitation accumulation in inches. */
    double precip_in;
};

/** A handle to an object that stores observations.
 *
 * The store may have the data stored locally, or it may request more data over the internet if
 * needed.
 */
typedef struct ObsStore ObsStore;

/** Connect to the default \c ObsStore.
 *
 * \param synoptic_labs_api_key is a \c NULL terminated string to a key for working with the
 * SynopticLabs API. This will be stored as an alias, so the argument must not be freed before the
 * returned ObsStore object is destroyed with obs_close().
 *
 * \returns an opaque pointer to the store. If there is a failure it will return \c NULL.
 */
ObsStore *obs_connect(char const *const synoptic_labs_api_key);

/** Close the connection to the observation store performing any necessary cleanup. */
void obs_close(ObsStore **store);

/** Get the daily maximum temperatures.
 *
 * \param store the data store to query.
 * \param site is the site identifier.
 * \param start_time is the beginning of the first window.
 * \param end_time - no window will START after this time, though one may end after it.
 * \param window_length - the window length in hours.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak. It's
 * the user's responsibility to manage this memory, this function will not free it for you.
 * \param num_results will be the number of TemperatureOb objects stored in \a results. This must
 * be 0 when passed in so it is consistent with the length of \a results.
 *
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the maximum temperature within a window of \c window_length. The first
 * window starts at \c start_time, and each subsequent window starts 24 hours later.
 */
int obs_query_max_t(ObsStore *store, char const *const site, time_t start_time, time_t end_time,
                    unsigned window_length, struct TemperatureOb **results, size_t *num_results);

/** Get the daily minimum temperatures.
 *
 * \param store the data store to query.
 * \param site is the site identifier.
 * \param start_time is the beginning of the first window.
 * \param end_time - no window will START after this time, though one may end after it.
 * \param window_length - the window length in hours.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak. It's
 * the user's responsibility to manage this memory, this function will not free it for you.
 * \param num_results will be the number of TemperatureOb objects stored in \a results. This must
 * be 0 when passed in so it is consistent with the length of \a results.
 *
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the minimum temperature within a window of \c window_length. The first
 * window starts at \c start_time, and each subsequent window starts 24 hours later.
 */
int obs_query_min_t(ObsStore *store, char const *const site, time_t start_time, time_t end_time,
                    unsigned window_length, struct TemperatureOb **results, size_t *num_results);

/** Get the accumulated precipitation in inches.
 *
 * \param store the data store to query.
 * \param site is the site identifier.
 * \param start_time is the beginning of the first window.
 * \param end_time - no window will START after this time, though one may end after it.
 * \param window_length - the window length in hours.
 * \param window_increment - the time in hours between when windows start.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak. It's
 * the user's responsibility to manage this memory, this function will not free it for you.
 * \param num_results will be the number of PrecipitationOb objects stored in \a results. This must
 * be 0 when passed in so it is consistent with the length of \a results.
 *
 *
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the accumulated precipitation within a window of \c window_length. The
 * first window starts at \c start_time, and each subsequent window starts \c window_increment
 * hours later.
 */
int obs_query_precipitation(ObsStore *store, char const *const site, time_t start_time,
                            time_t end_time, unsigned window_length, unsigned window_increment,
                            struct PrecipitationOb **results, size_t *num_results);
