#include "perf.h"
#include <gb/hardware.h>

static uint16_t perf_sum[PERF_METRIC_COUNT];
static uint16_t perf_count[PERF_METRIC_COUNT];
static uint8_t perf_peak[PERF_METRIC_COUNT];

uint8_t perf_stamp_now(void) {
    return DIV_REG;
}

uint8_t perf_stamp_elapsed(uint8_t *stamp) {
    uint8_t now = DIV_REG;
    uint8_t dt = (uint8_t)(now - *stamp);
    *stamp = now;
    return dt;
}

void perf_record(PerfMetric metric, uint8_t ticks) {
    if (metric >= PERF_METRIC_COUNT) return;
    if (perf_count[metric] < 0xFFFFu) perf_count[metric]++;
    if ((uint16_t)(0xFFFFu - perf_sum[metric]) < ticks) perf_sum[metric] = 0xFFFFu;
    else perf_sum[metric] = (uint16_t)(perf_sum[metric] + ticks);
    if (ticks > perf_peak[metric]) perf_peak[metric] = ticks;
}

uint8_t perf_avg(PerfMetric metric) {
    if (metric >= PERF_METRIC_COUNT || perf_count[metric] == 0u) return 0u;
    return (uint8_t)(perf_sum[metric] / perf_count[metric]);
}

uint8_t perf_max(PerfMetric metric) {
    if (metric >= PERF_METRIC_COUNT) return 0u;
    return perf_peak[metric];
}

void perf_clear_all(void) {
    uint8_t i;
    for (i = 0u; i < PERF_METRIC_COUNT; i++) {
        perf_sum[i] = 0u;
        perf_count[i] = 0u;
        perf_peak[i] = 0u;
    }
}
