#include "system_info.h"
#include "protocol.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum mon_command system_command(const char *request)
{
    static const char *const names[] = {"", "GET_STATUS", "GET_CPU", "GET_MEM", "GET_UPTIME"};
    for (int i = CMD_STATUS; i <= CMD_UPTIME; ++i) {
#ifdef MONITOR_TEST_BASELINE
        /* 對照版本只示範前綴誤判；不使用越界或記憶體破壞。 */
        if (strncmp(request, names[i], strlen(names[i])) == 0) return (enum mon_command)i;
#else
        if (strcmp(request, names[i]) == 0) return (enum mon_command)i;
#endif
    }
    return CMD_INVALID;
}

struct cpu_ticks { unsigned long long total, idle; };
static int read_cpu(struct cpu_ticks *ticks)
{
    FILE *file = fopen("/proc/stat", "r");
    if (!file) return -1;
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    int n = fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(file);
    if (n != 8) return -1;
    /* guest 已包含在 user／nice，不重複加總；iowait 歸入閒置。 */
    ticks->total = user + nice + system + idle + iowait + irq + softirq + steal;
    ticks->idle = idle + iowait;
    return 0;
}

static int cpu_usage(double *percent, long long *sample_ms)
{
    struct cpu_ticks first, second;
    if (read_cpu(&first) < 0) return -1;
    int64_t start = mon_now_ms();
    struct timespec remaining = {.tv_sec = 0, .tv_nsec = 200000000};
    while (nanosleep(&remaining, &remaining) < 0) {
        if (errno != EINTR || mon_stop) return -1;
    }
    if (mon_stop || read_cpu(&second) < 0) return -1;
    if (second.total <= first.total || second.idle < first.idle) return -1;
    unsigned long long total = second.total - first.total;
    unsigned long long idle = second.idle - first.idle;
    if (idle > total) return -1;
    *percent = 100.0 * (double)(total - idle) / (double)total;
    *sample_ms = (long long)(mon_now_ms() - start);
    return 0;
}

static int memory_info(unsigned long long *total, unsigned long long *available)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) return -1;
    char line[256];
    int found_total = 0, found_available = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "MemTotal: %llu kB", total) == 1) found_total = 1;
        if (sscanf(line, "MemAvailable: %llu kB", available) == 1) found_available = 1;
    }
    int failed = ferror(file);
    fclose(file);
    return !failed && found_total && found_available && *total && *available <= *total ? 0 : -1;
}

static int uptime_info(double *seconds)
{
    FILE *file = fopen("/proc/uptime", "r");
    if (!file) return -1;
    int n = fscanf(file, "%lf", seconds);
    fclose(file);
    return n == 1 && *seconds >= 0 ? 0 : -1;
}

int system_response(enum mon_command command, char *response, size_t capacity)
{
    double cpu = 0, uptime = 0;
    long long sample_ms = 0;
    unsigned long long total = 0, available = 0;
    if ((command == CMD_CPU || command == CMD_STATUS) && cpu_usage(&cpu, &sample_ms) < 0) return -1;
    if ((command == CMD_MEM || command == CMD_STATUS) && memory_info(&total, &available) < 0) return -1;
    if ((command == CMD_UPTIME || command == CMD_STATUS) && uptime_info(&uptime) < 0) return -1;
    unsigned long long used = total - available;
    double used_pct = total ? 100.0 * (double)used / (double)total : 0;
    int n;
    switch (command) {
    case CMD_CPU:
        n = snprintf(response, capacity, "OK CPU usage_pct=%.2f sample_ms=%lld\n", cpu, sample_ms); break;
    case CMD_MEM:
        n = snprintf(response, capacity, "OK MEM total_kb=%llu available_kb=%llu used_kb=%llu used_pct=%.2f\n",
                     total, available, used, used_pct); break;
    case CMD_UPTIME:
        n = snprintf(response, capacity, "OK UPTIME seconds=%.2f\n", uptime); break;
    case CMD_STATUS:
        n = snprintf(response, capacity,
                     "OK STATUS cpu_pct=%.2f sample_ms=%lld total_kb=%llu available_kb=%llu used_kb=%llu used_pct=%.2f uptime_seconds=%.2f\n",
                     cpu, sample_ms, total, available, used, used_pct, uptime); break;
    default: return -1;
    }
    return n < 0 || (size_t)n >= capacity ? -1 : 0;
}
