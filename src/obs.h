#pragma once
/** \file obs.h
 *
 * API for the obsdb or weather observation archive.
 *
 * This library serves as a library and archive for weather observations. It will download data if
 * necessary and back fill the archive.
 */

#include <time.h>

/** A time range. */
struct ObsTimeRange {
    time_t start; /**< The starting time, must be less than or equal to \ref end. */
    time_t end;   /**< The ending time, must be greater than or equal to \ref start. */
};

/** A temperature observation. */
struct ObsTemperature {
    /** Valid time of the observation.
     *
     * If the observation is valid for a specific time, the the valid time will have that time. If
     * it is valid for a period, for instance if it is a daily maximum or minumum temperature, the
     * valid time is for the time at the end of the valid period. The length of the valid period
     * will have to be deduced from the function call that created this object.
     */
    time_t valid_time;

    /** The temperature in Fahrenheit. */
    double temperature_f;
};

/** A precipitation observation. */
struct ObsPrecipitation {
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

/** Initialize a \ref ObsTimeRange
 *
 * \param tr is the object to initialize.
 * \param start is the start time, it must be less than or equal to \a end.
 * \param end is the end time, it must be greater than or equal to \a start.
 *
 * \returns a pointer to the initialized object, or \c NULL if there was an error. The only
 * conceivable error at the time of writing is that \a start > \a end.
 */
struct ObsTimeRange *obs_time_range_init(struct ObsTimeRange *tr, time_t start, time_t end);

/** Print a \ref ObsTimeRange.
 *
 * Meant for debugging mostly.
 */
void obs_time_range_print(struct ObsTimeRange tr);

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
 * \param time_range is the \ref ObsTimeRange which the end time of all windows will fall into.
 * \param window_end the UTC hour of the day that the observation window ends.
 * \param window_length - the window length in hours.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak.
 * \param num_results will be the number of \ref ObsTemperature objects stored in \a results. This
 * must be 0 when passed in so it is consistent with the length of \a results.
 *
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the maximum temperature within a window of \a window_length and the
 * time at the END of that window. All end times falling with \a time_range are returned.
 */
int obs_query_max_t(ObsStore *store, char const *const site, struct ObsTimeRange time_range,
                    unsigned window_end, unsigned window_length, struct ObsTemperature **results,
                    size_t *num_results);

/** Get the daily minimum temperatures.
 *
 * \param store the data store to query.
 * \param site is the site identifier.
 * \param time_range is the \ref ObsTimeRange which the end time of all windows will fall into.
 * \param window_end the UTC hour of the day that the observation window ends.
 * \param window_length the window length in hours.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak.
 * \param num_results will be the number of \ref ObsTemperature objects stored in \a results. This
 * must be 0 when passed in so it is consistent with the length of \a results.
 *
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the maximum temperature within a window of \a window_length and the
 * time at the END of that window. All end times falling with \a time_range are returned.
 */
int obs_query_min_t(ObsStore *store, char const *const site, struct ObsTimeRange time_range,
                    unsigned window_end, unsigned window_length, struct ObsTemperature **results,
                    size_t *num_results);

/** Get the accumulated precipitation in inches.
 *
 * \param store the data store to query.
 * \param site is the site identifier.
 * \param window_length - the window length in hours.
 * \param time_range is the \ref ObsTimeRange which the end time of all windows will fall into.
 * \param window_increment the time in hours between when windows start.
 * \param window_offset is the number of hours offset from 00Z the first ends.
 * \param results will be stored in an array returned here. This returned array will need to be
 * freed with \c free(). It must be \c NULL when passed in to ensure there is no memory leak.
 * \param num_results will be the number of \ref ObsPrecipitation objects stored in \a results.
 * This must be 0 when passed in so it is consistent with the length of \a results.
 *
 * \returns 0 on success, or a negative number upon failure.
 *
 * The returned values are the accumulated precipitation within a window of \a window_length. The
 * first window ends at \a window_offset or \a window_offset plus enough \a window_increments to
 * get it the shorttest time possible after the start of \a time_range, and each subsequent window
 * ends \a window_increment hours later.
 *
 * So if \a time_range starts at 12Z on a day, and \a window_offset is 7, then 13Z that day will be
 * the end time of the first window.
 */
int obs_query_precipitation(ObsStore *store, char const *const site, struct ObsTimeRange time_range,
                            unsigned window_length, unsigned window_increment,
                            unsigned window_offset, struct ObsPrecipitation **results,
                            size_t *num_results);
