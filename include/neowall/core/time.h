/* neowall/core/time.h — monotonic clock (src/core/log.c). */
#ifndef NEOWALL_CORE_TIME_H
#define NEOWALL_CORE_TIME_H

#include <stdint.h>

/* CLOCK_MONOTONIC in ms / µs, same epoch. */
uint64_t get_time_ms(void);
uint64_t get_time_us(void);

#endif /* NEOWALL_CORE_TIME_H */
