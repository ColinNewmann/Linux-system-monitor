#include "logging.h"
#include "protocol.h"
#include "system_info.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_CHILDREN 128
static int signal_pipe[2] = {-1, -1};
static pid_t children[MAX_CHILDREN];
static volatile sig_atomic_t shutting_down;

static void parent_signal(int signal_number)
{
    int saved = errno;
    if (signal_number != SIGCHLD) shutting_down = 1;
    unsigned char byte = (unsigned char)signal_number;
    ssize_t ignored = write(signal_pipe[1], &byte, 1);
    (void)ignored;
    errno = saved;
}

static void child_signal(int signal_number)
{
    (void)signal_number;
    mon_stop = 1;
}

static int install_signal(int number, void (*handler)(int))
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    return sigaction(number, &action, NULL);
}

static int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void reap_children(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (size_t i = 0; i < MAX_CHILDREN; ++i) if (children[i] == pid) children[i] = 0;
        if (WIFSIGNALED(status)) LOG_ERROR("child_reaped child=%ld signal=%d", (long)pid, WTERMSIG(status));
        else LOG_INFO("child_reaped child=%ld exit=%d", (long)pid, WEXITSTATUS(status));
    }
}

static int active_children(void)
{
    int count = 0;
    for (size_t i = 0; i < MAX_CHILDREN; ++i) if (children[i]) ++count;
    return count;
}

static void shutdown_children(void)
{
    for (size_t i = 0; i < MAX_CHILDREN; ++i) if (children[i]) kill(children[i], SIGTERM);
    int64_t deadline = mon_now_ms() + 2000;
    while (active_children() && mon_now_ms() < deadline) {
        reap_children();
        struct pollfd event = {.fd = signal_pipe[0], .events = POLLIN};
        poll(&event, 1, 50);
        unsigned char bytes[128];
        while (read(signal_pipe[0], bytes, sizeof(bytes)) > 0) {}
    }
    for (size_t i = 0; i < MAX_CHILDREN; ++i) if (children[i]) kill(children[i], SIGKILL);
    /* 關機階段確實等待所有子程序，避免遺留孤兒程序。 */
    for (size_t i = 0; i < MAX_CHILDREN; ++i) {
        if (!children[i]) continue;
        while (waitpid(children[i], NULL, 0) < 0 && errno == EINTR) {}
        children[i] = 0;
    }
}

static int send_response(int fd, const char *response, int timeout_ms)
{
    int result = mon_send_all(fd, response, strlen(response), timeout_ms);
    if (result != MON_LINE && result != MON_CANCELLED) {
        if (result == MON_TIMEOUT) LOG_ERROR("send_timeout");
        else LOG_ERROR("send_failed errno=%d message=%s", errno, strerror(errno));
    }
    return result;
}

static void serve_client(int fd, int timeout_ms)
{
    LOG_INFO("connected");
    int read_timeout = timeout_ms;
#ifdef MONITOR_TEST_BASELINE
    read_timeout = -1;
#endif
    while (!mon_stop) {
        char request[MON_REQUEST_MAX + 1], response[MON_RESPONSE_MAX + 1];
        int result = mon_recv_line(fd, request, MON_REQUEST_MAX, read_timeout);
        if (result == MON_EOF || result == MON_CANCELLED) break;
#ifdef MONITOR_TEST_BASELINE
        /* 超長資料只取已安全儲存的前綴，示範錯誤接受；回應後關閉。 */
        int baseline_oversized = result == MON_TOO_LONG;
        if (baseline_oversized) result = MON_LINE;
#endif
        if (result != MON_LINE) {
            const char *error = "ERR IO_ERROR\n";
            if (result == MON_TIMEOUT) error = "ERR TIMEOUT\n";
            if (result == MON_TOO_LONG) error = "ERR REQUEST_TOO_LONG\n";
            if (result == MON_INVALID || result == MON_TRUNCATED) error = "ERR INVALID_REQUEST\n";
            LOG_ERROR("request_rejected result=%d", result);
            send_response(fd, error, timeout_ms);
            break;
        }
#if MONITOR_DEBUG
        int64_t start = mon_now_ms();
#endif
        LOG_DEBUG("request_start mono_ms=%lld command=%s", (long long)start, request);
        enum mon_command command = system_command(request);
        if (command == CMD_INVALID) {
            snprintf(response, sizeof(response), "ERR INVALID_REQUEST\n");
            LOG_ERROR("invalid_command");
        } else if (system_response(command, response, sizeof(response)) < 0) {
            if (mon_stop) break;
            snprintf(response, sizeof(response), "ERR SYSTEM_INFO\n");
            LOG_ERROR("system_info_failed");
        }
        if (send_response(fd, response, timeout_ms) != MON_LINE) break;
        LOG_DEBUG("request_end mono_ms=%lld elapsed_ms=%lld", (long long)mon_now_ms(),
                  (long long)(mon_now_ms() - start));
#ifdef MONITOR_TEST_BASELINE
        if (baseline_oversized) break;
#endif
    }
    close(fd);
    LOG_INFO("disconnected");
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--bind IPv4] [--port 1..65535] [--log-level 0..2] [--timeout-ms 100..600000]\n", program);
}

int main(int argc, char **argv)
{
    const char *bind_address = "127.0.0.1";
    int port = 8080, level = 1, timeout_ms = 10000;
    static const struct option options[] = {
        {"bind", required_argument, NULL, 'b'}, {"port", required_argument, NULL, 'p'},
        {"log-level", required_argument, NULL, 'l'}, {"timeout-ms", required_argument, NULL, 't'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    int option;
    while ((option = getopt_long(argc, argv, "b:p:l:t:h", options, NULL)) != -1) {
        switch (option) {
        case 'b': bind_address = optarg; break;
        case 'p': if (mon_parse_int(optarg, 1, 65535, &port)) goto invalid; break;
        case 'l': if (mon_parse_int(optarg, 0, 2, &level)) goto invalid; break;
        case 't': if (mon_parse_int(optarg, 100, 600000, &timeout_ms)) goto invalid; break;
        case 'h': usage(argv[0]); return 0;
        default: goto invalid;
        }
    }
    if (optind != argc) goto invalid;
    mon_log_init("parent", level);
#ifdef MONITOR_TEST_BASELINE
    LOG_ERROR("TEST BASELINE: protections intentionally disabled; loopback only");
    if (strcmp(bind_address, "127.0.0.1") != 0) goto invalid;
#endif
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((unsigned short)port)};
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) goto invalid;
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) { LOG_ERROR("socket: %s", strerror(errno)); return 1; }
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(listener, 64) < 0 || make_nonblocking(listener) < 0) {
        LOG_ERROR("listen setup: %s", strerror(errno)); close(listener); return 1;
    }
    if (pipe(signal_pipe) < 0) { LOG_ERROR("pipe: %s", strerror(errno)); close(listener); return 1; }
    if (make_nonblocking(signal_pipe[0]) < 0 || make_nonblocking(signal_pipe[1]) < 0 ||
        install_signal(SIGCHLD, parent_signal) < 0 || install_signal(SIGINT, parent_signal) < 0 ||
        install_signal(SIGTERM, parent_signal) < 0 || install_signal(SIGPIPE, SIG_DFL) < 0) {
        LOG_ERROR("signal setup: %s", strerror(errno));
        close(listener); close(signal_pipe[0]); close(signal_pipe[1]); return 1;
    }
    LOG_INFO("listening address=%s port=%d timeout_ms=%d", bind_address, port, timeout_ms);
    LOG_DEBUG("debug_enabled");
    int exit_code = 0;
    while (!shutting_down) {
        reap_children();
        struct pollfd events[2] = {{.fd = listener, .events = POLLIN},
                                   {.fd = signal_pipe[0], .events = POLLIN}};
        int ready = poll(events, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("poll: %s", strerror(errno)); exit_code = 1; break;
        }
        if (events[1].revents) {
            unsigned char bytes[128];
            while (read(signal_pipe[0], bytes, sizeof(bytes)) > 0) {}
            reap_children();
        }
        if (shutting_down || !(events[0].revents & POLLIN)) continue;
        int connection = accept(listener, NULL, NULL);
        if (connection < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("accept: %s", strerror(errno));
                /* 資源不足時退出並清理，避免反覆 accept 形成 busy loop。 */
                exit_code = 1; break;
            }
            continue;
        }
        size_t slot = 0;
        while (slot < MAX_CHILDREN && children[slot]) ++slot;
        if (slot == MAX_CHILDREN) { LOG_ERROR("connection_limit"); close(connection); continue; }
        sigset_t blocked, previous;
        sigemptyset(&blocked); sigaddset(&blocked, SIGINT); sigaddset(&blocked, SIGTERM); sigaddset(&blocked, SIGCHLD);
        if (sigprocmask(SIG_BLOCK, &blocked, &previous) < 0) {
            LOG_ERROR("sigprocmask: %s", strerror(errno)); close(connection); exit_code = 1; break;
        }
        pid_t child = fork();
        if (child == 0) {
            close(listener); close(signal_pipe[0]); close(signal_pipe[1]);
            if (install_signal(SIGCHLD, SIG_DFL) < 0 || install_signal(SIGINT, child_signal) < 0 ||
                install_signal(SIGTERM, child_signal) < 0 || sigprocmask(SIG_SETMASK, &previous, NULL) < 0) {
                close(connection); _exit(1);
            }
            mon_log_init("child", level);
            serve_client(connection, timeout_ms);
            _exit(0);
        }
        if (child < 0) LOG_ERROR("fork: %s", strerror(errno));
        else { children[slot] = child; LOG_INFO("child_created child=%ld", (long)child); }
        close(connection);
        if (sigprocmask(SIG_SETMASK, &previous, NULL) < 0) { exit_code = 1; break; }
    }
    close(listener);
    shutdown_children();
    LOG_INFO("shutdown_complete active=%d", active_children());
    close(signal_pipe[0]); close(signal_pipe[1]);
    return exit_code;
invalid:
    usage(argv[0]); return 2;
}
