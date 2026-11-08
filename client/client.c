#include "logging.h"
#include "protocol.h"
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void stop_signal(int number) { (void)number; mon_stop = 1; }

static void usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s [--host numeric-IP] [--port 1..65535] [--log-level 0..2]\n"
        "  [--command GET_STATUS|GET_CPU|GET_MEM|GET_UPTIME]\n"
        "  [--monitor] [--interval-ms 100..600000] [--count N] [--timeout-ms 100..600000]\n"
        "Without --command/--monitor, an interactive menu is shown.\n", program);
}

static int query(int fd, const char *command, int timeout_ms)
{
    char request[MON_REQUEST_MAX + 1], response[MON_RESPONSE_MAX + 1];
    int n = snprintf(request, sizeof(request), "%s\n", command);
    if (n < 0 || n > MON_REQUEST_MAX) { LOG_ERROR("command too long"); return -1; }
    LOG_DEBUG("sending command=%s", command);
    int result = mon_send_all(fd, request, (size_t)n, timeout_ms);
    if (result == MON_LINE) result = mon_recv_line(fd, response, MON_RESPONSE_MAX, timeout_ms);
    if (result != MON_LINE) {
        if (!mon_stop) LOG_ERROR("query_failed result=%d errno=%d message=%s", result, errno, strerror(errno));
        return -1;
    }
    puts(response);
    fflush(stdout);
    LOG_DEBUG("response_received");
    if (strncmp(response, "OK ", 3) != 0) { LOG_ERROR("server response=%s", response); return -1; }
    return 0;
}

static int monitor(int fd, int interval_ms, int count, int timeout_ms)
{
    int completed = 0;
    while (!mon_stop && (count == 0 || completed < count)) {
        int64_t next = mon_now_ms() + interval_ms;
        if (query(fd, "GET_STATUS", timeout_ms) < 0) return mon_stop ? 0 : -1;
        ++completed;
        if (count && completed >= count) break;
        /* 使用 monotonic deadline；取樣耗時算入更新間隔。 */
        while (!mon_stop && mon_now_ms() < next) {
            int64_t left = next - mon_now_ms();
            if (left <= 0) break;
            poll(NULL, 0, (int)left);
        }
    }
    return 0;
}

static int menu(int fd, int interval_ms, int count, int timeout_ms)
{
    while (!mon_stop) {
        printf("\nLINUX SYSTEM MONITOR\n\n"
               "1. Show System Overview\n2. Show CPU Usage\n3. Show Memory Usage\n"
               "4. Show System Uptime\n5. Continuous Monitoring (Ctrl+C to exit)\n6. Exit\n> ");
        fflush(stdout);
        /* 逐字元讀取避免 stdio 預讀與 poll 的 readiness 不一致。 */
        char input[32];
        size_t used = 0;
        int too_long = 0;
        for (;;) {
            int ready = mon_wait(STDIN_FILENO, POLLIN, -1);
            if (ready != MON_LINE) return mon_stop ? 0 : -1;
            char c;
            ssize_t n = read(STDIN_FILENO, &c, 1);
            if (n == 0) return 0;
            if (n < 0) { if (errno == EINTR) continue; return -1; }
            if (c == '\n') break;
            if (used < sizeof(input) - 1) input[used++] = c;
            else too_long = 1;
        }
        input[used] = '\0';
        int choice;
        if (too_long || mon_parse_int(input, 1, 6, &choice) < 0) { puts("Please choose 1 through 6."); continue; }
        if (choice == 6) return 0;
        if (choice == 5) return monitor(fd, interval_ms, count, timeout_ms);
        static const char *const commands[] = {"", "GET_STATUS", "GET_CPU", "GET_MEM", "GET_UPTIME"};
        if (query(fd, commands[choice], timeout_ms) < 0) return mon_stop ? 0 : -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1", *port = "8080", *command = NULL;
    int level = 1, timeout_ms = 10000, interval_ms = 2000, count = 0, continuous = 0;
    static const struct option options[] = {
        {"host", required_argument, NULL, 'H'}, {"port", required_argument, NULL, 'p'},
        {"log-level", required_argument, NULL, 'l'}, {"timeout-ms", required_argument, NULL, 't'},
        {"command", required_argument, NULL, 'c'}, {"monitor", no_argument, NULL, 'm'},
        {"interval-ms", required_argument, NULL, 'i'}, {"count", required_argument, NULL, 'n'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    int option, port_number;
    while ((option = getopt_long(argc, argv, "H:p:l:t:c:mi:n:h", options, NULL)) != -1) {
        switch (option) {
        case 'H': host = optarg; break;
        case 'p': if (mon_parse_int(optarg, 1, 65535, &port_number)) goto invalid; port = optarg; break;
        case 'l': if (mon_parse_int(optarg, 0, 2, &level)) goto invalid; break;
        case 't': if (mon_parse_int(optarg, 100, 600000, &timeout_ms)) goto invalid; break;
        case 'c': command = optarg; break;
        case 'm': continuous = 1; break;
        case 'i': if (mon_parse_int(optarg, 100, 600000, &interval_ms)) goto invalid; break;
        case 'n': if (mon_parse_int(optarg, 1, 1000000, &count)) goto invalid; break;
        case 'h': usage(argv[0]); return 0;
        default: goto invalid;
        }
    }
    if (optind != argc || (command && continuous)) goto invalid;
    mon_log_init("client", level);
    struct sigaction action;
    memset(&action, 0, sizeof(action)); action.sa_handler = stop_signal; sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        LOG_ERROR("signal: %s", strerror(errno)); return 1;
    }
    int fd = mon_connect(host, port, timeout_ms);
    if (fd < 0) { LOG_ERROR("connect: %s", strerror(errno)); return mon_stop ? 0 : 1; }
    LOG_INFO("connected host=%s port=%s", host, port);
    if (!command && !continuous) printf("Connected to %s:%s\n", host, port);
    int result = command ? query(fd, command, timeout_ms) :
                 continuous ? monitor(fd, interval_ms, count, timeout_ms) :
                 menu(fd, interval_ms, count, timeout_ms);
    close(fd);
    LOG_INFO("disconnected");
    return mon_stop ? 0 : result == 0 ? 0 : 1;
invalid:
    usage(argv[0]); return 2;
}
