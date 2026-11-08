#include "protocol.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    const size_t length = 1024 * 1024;
    char *payload = malloc(length);
    if (!payload) return 2;
    memset(payload, 'Q', length);
    int pair[2], small_buffer = 4096;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) { perror("socketpair"); free(payload); return 2; }
    if (setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small_buffer, sizeof(small_buffer)) < 0) return 2;
    pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
        close(pair[0]);
        size_t received = 0;
        char block[997];
        for (;;) {
            ssize_t n = read(pair[1], block, sizeof(block));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) _exit(3);
            if (n == 0) break;
            for (ssize_t i = 0; i < n; ++i) if (block[i] != 'Q') _exit(4);
            received += (size_t)n;
        }
        close(pair[1]);
        free(payload);
        _exit(received == length ? 0 : 5);
    }
    close(pair[1]);
    int result = mon_send_all(pair[0], payload, length, 3000);
    close(pair[0]);
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 2;
    if (result != MON_LINE || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 1;
    printf("partial writes: sent and verified %zu bytes; SO_SNDBUF requested=%d\n", length, small_buffer);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) return 2;
    if (setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small_buffer, sizeof(small_buffer)) < 0) return 2;
    int64_t start = mon_now_ms();
    result = mon_send_all(pair[0], payload, length, 100);
    long long elapsed = (long long)(mon_now_ms() - start);
    close(pair[0]); close(pair[1]); free(payload);
    printf("blocked writer: result=%d elapsed_ms=%lld\n", result, elapsed);
    return result == MON_TIMEOUT && elapsed >= 90 && elapsed < 1500 ? 0 : 1;
}
