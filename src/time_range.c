/** \file time_range.c
 *
 * \brief Implementation of the TimeRange type.
 */

#include "obs.h"

#include <stdio.h>

struct ObsTimeRange *
obs_time_range_init(struct ObsTimeRange *tr, time_t start, time_t end)
{
    if (start > end) {
        *tr = (struct ObsTimeRange){0};
        return 0;
    }

    tr->start = start;
    tr->end = end;

    return tr;
}

void
obs_time_range_print(struct ObsTimeRange tr)
{
    struct tm start = *gmtime(&tr.start);
    struct tm end = *gmtime(&tr.end);

    char start_buf[128] = {0};
    char end_buf[128] = {0};

    strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H%M", &start);
    strftime(end_buf, sizeof(end_buf), "%Y-%m-%d %H%M", &end);

    printf("TimeRange [%s -> %s]\n", start_buf, end_buf);
}
