/** \file download.c
 * \brief Implementation of the download module.
 */
#include "download.h"
#include "obs_db.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <tgmath.h>

#include <csv.h>
#include <curl/curl.h>
#include <sqlite3.h>

/*-------------------------------------------------------------------------------------------------
 *                       CSV Parsers and insert into sqlite database.
 *-----------------------------------------------------------------------------------------------*/
/** Holds state for callbacks for libcsv, which are passing data to sqlite3.*/
struct CsvToSqliteState {
    sqlite3_stmt *insert_stmt; /**< The database we'll be storing this into. */

    bool header_parsed; /**< Has the header row been parsed? So we have values for vt_col, t_col */
    size_t col;         /**< Current column. */

    size_t vt_col; /**< The column number for the valid time. */
    size_t t_col;  /**< The column number for the temperature data. */
    size_t p_col;  /**< The column number for the precipitation data. */

    time_t valid_time; /**< The valid time of the observation. */
    char const *site;  /**< The site name from the query, this is an alias, do not free. */
    double t_f;        /**< Temperature in Fahrenheit. */
    double p_in;       /**< Precipitation in inches. */

    bool error; /**< Whether an error has occurred. */
};

static struct CsvToSqliteState
obs_download_init_csv_state(sqlite3 *local_store, char const *site_id)
{

    int rc = obs_db_start_transaction(local_store);
    StopIf(rc, goto ERR_RETURN, "error starting transaction");

    sqlite3_stmt *insert_stmt = obs_db_create_insert_statement(local_store);
    StopIf(!insert_stmt, goto ERR_RETURN, "error creating insert statement");

    return (struct CsvToSqliteState){.insert_stmt = insert_stmt,
                                     .header_parsed = false,
                                     .col = 0,
                                     .t_col = 0,
                                     .p_col = 0,
                                     .valid_time = 0,
                                     .site = site_id,
                                     .t_f = NAN,
                                     .p_in = NAN,
                                     .error = false};

ERR_RETURN:

    return (struct CsvToSqliteState){.error = true};
}

static int
obs_download_finalize_csv_state(sqlite3 *local_store, struct CsvToSqliteState *csv_state)
{
    obs_db_finalize_insert_statement(csv_state->insert_stmt);

    int action = OBS_DB_TRANSACTION_COMMIT;
    if (csv_state->error) {
        action = OBS_DB_TRANSACTION_ROLLBACK;
    }

    int rc = obs_db_finish_transaction(local_store, action);

    // Return 0 if everything went well, a negative value otherwise
    return rc;
}

static double
col_callback_parse_double(char const *txt, struct CsvToSqliteState *st)
{
    if (!txt) {
        goto ERR_RETURN;
    }

    char *end = 0;
    double value = strtod(txt, &end);

    if (end == txt) {
        goto ERR_RETURN;
    }

    return value;

ERR_RETURN:
    st->error = true;
    return NAN;
}

static double
col_callback_parse_double_missing_is_zero(char const *txt, struct CsvToSqliteState *st)
{
    if (!txt) {
        return 0.0;
    }

    char *end = 0;
    double value = strtod(txt, &end);

    if (end == txt) {
        goto ERR_RETURN;
    }

    return value;

ERR_RETURN:
    st->error = true;
    return NAN;
}

static time_t
col_callback_parse_valid_time(char const *txt, struct CsvToSqliteState *st)
{
    if (!txt) {
        goto ERR_RETURN;
    }

    struct tm parsed_tm = {0};
    time_t parsed_time = 0;
    char *parse_res = strptime(txt, "%Y-%m-%dT%H:%M:%SZ", &parsed_tm);
    if (parse_res) {
        parsed_time = timegm(&parsed_tm);
    } else {
        goto ERR_RETURN;
    }

    return parsed_time;

ERR_RETURN:
    st->error = true;
    return 0;
}

static void
col_callback_parse_col_header(char const *txt, struct CsvToSqliteState *st)
{
    if (txt) {
        if (strstr(txt, "Date_Time") != 0) {
            st->vt_col = st->col;
        } else if (strstr(txt, "air_temp_set_1") != 0) {
            st->t_col = st->col;
        } else if (strstr(txt, "precip_accum_one_hour_set_1") != 0) {
            st->p_col = st->col;
        }

        // Ignore other columns for now.
    }
}

static void
col_callback(void *data, size_t num_bytes, void *userdata)
{
    struct CsvToSqliteState *st = userdata;
    char *txt = data;

    // Skip (without counting) rows that start with #
    if (st->error || (txt && txt[0] == '#')) {
        st->error = true;
        return;
    }

    if (!st->header_parsed) {
        col_callback_parse_col_header(txt, st);
    } else {
        if (st->col == st->vt_col) {
            st->valid_time = col_callback_parse_valid_time(txt, st);
        } else if (st->col == st->t_col) {
            st->t_f = col_callback_parse_double(txt, st);
        } else if (st->col == st->p_col) {
            st->p_in = col_callback_parse_double_missing_is_zero(txt, st);
        }
    }

    st->col++;
    return;
}

static bool
row_callback_is_error_condition(struct CsvToSqliteState *st)
{
    return st->error || isnan(st->t_f) || isnan(st->p_in) || st->valid_time == 0;
}

static void
row_callback(int cause_char, void *userdata)
{
    struct CsvToSqliteState *st = userdata;

    if (!st->header_parsed && !st->error) {
        // We just updated the column header indexes so we know what column has which values.
        st->header_parsed = true;
    } else if (!row_callback_is_error_condition(st)) {
        // Ignore errors from this function and just keep going. The callback nature of
        // libcsv doesn't give us a way to abort even if we wanted to.
        obs_db_insert(st->insert_stmt, st->valid_time, st->site, st->t_f, st->p_in);
    }

    st->col = 0;

    st->valid_time = 0;
    st->t_f = NAN;
    st->p_in = NAN;
    st->error = false;

    return;
}

/*-------------------------------------------------------------------------------------------------
 *                       CURL state and callback for handling data.
 *-----------------------------------------------------------------------------------------------*/
/**Holds state for callbacks from cURL to libcsv. */
struct CurlToCsvState {
    struct CsvToSqliteState *csv_state; /**< State to pass through to the csv parser. */
    struct csv_parser parser;           /**< The csv parser. */

    bool error; /**< Has an error occurred yet in processing. */
};

static struct CurlToCsvState
obs_download_init_curl_state(struct CsvToSqliteState *csv_state)
{
    struct csv_parser csv = {0};
    int csv_code = csv_init(&csv, CSV_APPEND_NULL | CSV_EMPTY_IS_NULL);
    StopIf(csv_code != 0, goto ERR_RETURN, "error initializing libcsv");

    return (struct CurlToCsvState){.csv_state = csv_state, .parser = csv};

ERR_RETURN:

    return (struct CurlToCsvState){.error = true};
}

static void
obs_download_finalize_curl_state(struct CurlToCsvState *curl_state)
{
    csv_free(&curl_state->parser);
    curl_state->csv_state = 0;

    return;
}

static size_t
curl_callback(char *ptr, size_t size, size_t nmember, void *userdata)
{
    struct CurlToCsvState *curl_state = userdata;
    size_t num_bytes = size * nmember;

    size_t bytes_processed = csv_parse(&curl_state->parser, ptr, num_bytes, col_callback,
                                       row_callback, curl_state->csv_state);

    if (bytes_processed != num_bytes) {
        fprintf(stderr, "error parsing csv file: %s\n",
                csv_strerror(csv_error(&curl_state->parser)));
    }

    return bytes_processed;
}

/*-------------------------------------------------------------------------------------------------
 *                                         CURL set up.
 *-----------------------------------------------------------------------------------------------*/
static char *
obs_download_create_synoptic_labs_url(char const *const api_key, char const *site_id,
                                      struct ObsTimeRange tr)
{
    static char const *const base_url = "https://api.synopticdata.com/v2/stations/timeseries?"
                                        "stid=%s"
                                        "&vars=air_temp,precip_accum_one_hour&units=english"
                                        "&output=csv"
                                        "&start=%s&end=%s"
                                        "&hfmetars=0"
                                        "&token=%s";

    struct tm start_tm = *gmtime(&tr.start);
    struct tm end_tm = *gmtime(&tr.end);

    char start_str[16] = {0};
    char end_str[16] = {0};

    size_t num_chars = strftime(start_str, sizeof(start_str), "%Y%m%d%H%M", &start_tm);
    StopIf(num_chars == 0, exit(EXIT_FAILURE), "impossible memory error formatting time");
    num_chars = strftime(end_str, sizeof(end_str), "%Y%m%d%H%M", &end_tm);
    StopIf(num_chars == 0, exit(EXIT_FAILURE), "impossible memory error formatting time");

    char *url = 0;
    int num_printed = asprintf(&url, base_url, site_id, start_str, end_str, api_key);
    StopIf(num_printed < strlen(base_url), exit(EXIT_FAILURE), "memory allocation error!");

    return url;
}

static CURL *
obs_download_init_check_curl(CURL **curl, struct CurlToCsvState *curl_state)
{
    assert(curl);

    CURLcode res = 0;
    if (!*curl) {
        res = curl_global_init(CURL_GLOBAL_DEFAULT);
        StopIf(res, goto ERR_RETURN, "Failed to initialize curl");

        CURL *curl_init = curl_easy_init();
        StopIf(!curl, goto ERR_RETURN, "curl_easy_init failed.");

        res = curl_easy_setopt(curl_init, CURLOPT_FAILONERROR, true);
        StopIf(res, goto ERR_RETURN, "curl_easy_setopt failed to set fail on error.");

        res = curl_easy_setopt(curl_init, CURLOPT_WRITEFUNCTION, curl_callback);
        StopIf(res, goto ERR_RETURN, "curl_easy_setopt failed to set the write_callback.");

        res = curl_easy_setopt(curl_init, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        StopIf(res, goto ERR_RETURN, "curl_easy_setopt failed to set the user agent.");

        *curl = curl_init;
    }

    res = curl_easy_setopt(*curl, CURLOPT_WRITEDATA, curl_state);
    StopIf(res, goto ERR_RETURN, "curl_easy_setopt failed to set the user data.");

    return *curl;

ERR_RETURN:
    return 0;
}

/*-------------------------------------------------------------------------------------------------
 *                                        Module API function.
 *-----------------------------------------------------------------------------------------------*/
int
obs_download(sqlite3 *local_store, CURL **curl, char const *const synoptic_labs_api_key,
             char const *site_id, struct ObsTimeRange tr)
{
    int return_code = 0;

    char *url = 0;

    struct CsvToSqliteState csv_state = obs_download_init_csv_state(local_store, site_id);
    StopIf(csv_state.error, goto ERR_RETURN, "error initializing csv_state.");

    struct CurlToCsvState curl_state = obs_download_init_curl_state(&csv_state);
    StopIf(curl_state.error, goto ERR_RETURN, "error initializing cURL state.");

    CURL *c_handle = obs_download_init_check_curl(curl, &curl_state);
    StopIf(!c_handle, goto ERR_RETURN, "error initializing cURL");

    url = obs_download_create_synoptic_labs_url(synoptic_labs_api_key, site_id, tr);
    assert(url);

    int res = curl_easy_setopt(c_handle, CURLOPT_URL, url);
    StopIf(res, goto ERR_RETURN, "curl_easy_setopt failed to set the url.");

    res = curl_easy_perform(c_handle);
    StopIf(res, goto ERR_RETURN, "curl_easy_perform failed: %s", curl_easy_strerror(res));

RETURN:

    obs_download_finalize_curl_state(&curl_state);
    obs_download_finalize_csv_state(local_store, &csv_state);
    free(url);

    return return_code;

ERR_RETURN:
    return_code = -1;
    goto RETURN;
}
