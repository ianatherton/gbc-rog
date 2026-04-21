#ifndef PERF_H
#define PERF_H

#include <stdint.h>
#include <gbdk/platform.h>

typedef enum {
    PERF_ENEMY_MOVE = 0,
    PERF_CAMERA_SCROLL,
    PERF_DRAW_SCREEN,
    PERF_DRAW_OVERLAY,
    PERF_HIT_RESOLVE,
    PERF_METRIC_COUNT
} PerfMetric;

uint8_t perf_stamp_now(void);
uint8_t perf_stamp_elapsed(uint8_t *stamp);
void perf_record(PerfMetric metric, uint8_t ticks);
uint8_t perf_avg(PerfMetric metric);
uint8_t perf_max(PerfMetric metric);
void perf_clear_all(void);

#endif // PERF_H
