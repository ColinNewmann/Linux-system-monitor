#ifndef MONITOR_LOGGING_H
#define MONITOR_LOGGING_H

enum { MON_ERROR = 0, MON_INFO = 1, MON_DEBUG = 2 };
void mon_log_init(const char *role, int level);
void mon_log(int level, const char *format, ...) __attribute__((format(printf, 2, 3)));
#ifndef MONITOR_DEBUG
#define MONITOR_DEBUG 0
#endif
#if MONITOR_DEBUG
#define LOG_DEBUG(...) mon_log(MON_DEBUG, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif
#define LOG_INFO(...) mon_log(MON_INFO, __VA_ARGS__)
#define LOG_ERROR(...) mon_log(MON_ERROR, __VA_ARGS__)
#endif
