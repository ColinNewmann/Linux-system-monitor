#ifndef MONITOR_PROTOCOL_H
#define MONITOR_PROTOCOL_H
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

/* 長度包含換行；額外一個 byte 保留給 C 字串結尾。 */
#define MON_REQUEST_MAX 128
#define MON_RESPONSE_MAX 2048
enum mon_result {
    MON_LINE = 1, MON_EOF = 0, MON_IO_ERROR = -1, MON_TIMEOUT = -2,
    MON_TOO_LONG = -3, MON_INVALID = -4, MON_TRUNCATED = -5,
    MON_CANCELLED = -6
};
extern volatile sig_atomic_t mon_stop;
int64_t mon_now_ms(void);
int mon_wait(int fd, short events, int64_t deadline);
int mon_recv_line(int fd, char *buffer, size_t wire_limit, int timeout_ms);
int mon_send_all(int fd, const char *buffer, size_t length, int timeout_ms);
int mon_parse_int(const char *text, int minimum, int maximum, int *value);
int mon_connect(const char *host, const char *port, int timeout_ms);
#endif
