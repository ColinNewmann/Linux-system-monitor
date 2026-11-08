#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t mon_stop;

int64_t mon_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int mon_wait(int fd, short events, int64_t deadline)
{
    for (;;) {
        if (mon_stop) return MON_CANCELLED;
        /* 限制單次等待，避免訊號剛好在檢查旗標後到達造成漏喚醒。 */
        int wait_ms = 200;
        if (deadline >= 0) {
            int64_t left = deadline - mon_now_ms();
            if (left <= 0) return MON_TIMEOUT;
            wait_ms = left > 200 ? 200 : (int)left;
        }
        struct pollfd p = {.fd = fd, .events = events};
        int result = poll(&p, 1, wait_ms);
        if (result > 0) {
            if (p.revents & POLLNVAL) { errno = EBADF; return MON_IO_ERROR; }
            return MON_LINE; /* HUP／ERR 交由 recv／send 取得具體結果。 */
        }
        if (result == 0) continue;
        if (errno != EINTR) return MON_IO_ERROR;
    }
}

int mon_recv_line(int fd, char *buffer, size_t wire_limit, int timeout_ms)
{
    int64_t deadline = timeout_ms < 0 ? -1 : mon_now_ms() + timeout_ms;
    size_t used = 0;
    buffer[0] = '\0';
    for (;;) {
        int ready = mon_wait(fd, POLLIN, deadline);
        if (ready != MON_LINE) return ready;
        unsigned char c;
        ssize_t n = recv(fd, &c, 1, MSG_DONTWAIT);
        if (n == 0) return used ? MON_TRUNCATED : MON_EOF;
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return MON_IO_ERROR;
        }
        if (c == '\n') { buffer[used] = '\0'; return MON_LINE; }
        if (c == '\0' || c == '\r' || c < 32 || c > 126) return MON_INVALID;
        if (used >= wire_limit - 1) return MON_TOO_LONG;
        buffer[used++] = (char)c;
        buffer[used] = '\0';
    }
}

int mon_send_all(int fd, const char *buffer, size_t length, int timeout_ms)
{
    int64_t deadline = timeout_ms < 0 ? -1 : mon_now_ms() + timeout_ms;
    size_t sent = 0;
    while (sent < length) {
        int ready = mon_wait(fd, POLLOUT, deadline);
        if (ready != MON_LINE) return ready;
        int flags = MSG_DONTWAIT;
#ifndef MONITOR_TEST_BASELINE
        flags |= MSG_NOSIGNAL;
#endif
        ssize_t n = send(fd, buffer + sent, length - sent, flags);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return MON_IO_ERROR;
        }
        if (n == 0) { errno = EPIPE; return MON_IO_ERROR; }
        sent += (size_t)n;
    }
    return MON_LINE;
}

int mon_parse_int(const char *text, int minimum, int maximum, int *value)
{
    if (!text || !*text) return -1;
    for (const char *p = text; *p; ++p) if (*p < '0' || *p > '9') return -1;
    char *end;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno || *end || n < minimum || n > maximum) return -1;
    *value = (int)n;
    return 0;
}

int mon_connect(const char *host, const char *port, int timeout_ms)
{
    /* 僅接受數字位址，避免同步 DNS 解析突破連線期限。 */
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM,
                             .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV};
    struct addrinfo *addresses;
    if (getaddrinfo(host, port, &hints, &addresses) != 0) { errno = EINVAL; return -1; }
    int fd = -1, saved = ECONNREFUSED;
    int64_t deadline = mon_now_ms() + timeout_ms;
    for (struct addrinfo *a = addresses; a && !mon_stop; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) { saved = errno; continue; }
        int flags = fcntl(fd, F_GETFL);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            saved = errno; close(fd); fd = -1; continue;
        }
        int result = connect(fd, a->ai_addr, a->ai_addrlen);
        if (result < 0 && errno == EINPROGRESS) {
            int ready = mon_wait(fd, POLLOUT, deadline);
            if (ready == MON_LINE) {
                int error = 0;
                socklen_t size = sizeof(error);
                result = getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size);
                if (result == 0 && error) { errno = error; result = -1; }
            } else { errno = ready == MON_TIMEOUT ? ETIMEDOUT : EINTR; result = -1; }
        }
        if (result == 0) break;
        saved = errno; close(fd); fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) errno = saved;
    return fd;
}
