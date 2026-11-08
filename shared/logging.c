#include "logging.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static const char *log_role = "monitor";
static int log_level;

void mon_log_init(const char *role, int level)
{
    log_role = role;
    log_level = level;
}

void mon_log(int level, const char *format, ...)
{
    if (level > log_level) return;
    static const char *const names[] = {"ERROR", "INFO", "DEBUG"};
    char line[1024];
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    int prefix = snprintf(line, sizeof(line), "%lld.%03ld pid=%ld role=%s %s ",
                          (long long)now.tv_sec, now.tv_nsec / 1000000,
                          (long)getpid(), log_role, names[level]);
    if (prefix < 0 || (size_t)prefix >= sizeof(line) - 2) return;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix - 1, format, args);
    va_end(args);
    if (n < 0) return;
    size_t used = (size_t)prefix + (size_t)n;
    if (used > sizeof(line) - 2) used = sizeof(line) - 2;
    line[used++] = '\n';
    /* 單次寫出，降低多個子程序的日誌交錯機率。 */
    ssize_t ignored = write(STDERR_FILENO, line, used);
    (void)ignored;
}
