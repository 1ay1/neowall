/* neowall/log.h — leveled logging to stderr (src/utils.c). */
#ifndef NEOWALL_LOG_H
#define NEOWALL_LOG_H

#include <stdbool.h>

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

void log_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
void log_quiet_errors(bool quiet);
void log_warn(const char *format, ...) __attribute__((format(printf, 1, 2)));
void log_info(const char *format, ...) __attribute__((format(printf, 1, 2)));
void log_debug(const char *format, ...) __attribute__((format(printf, 1, 2)));
void log_set_level(int level);
int log_get_level(void);

#endif /* NEOWALL_LOG_H */
