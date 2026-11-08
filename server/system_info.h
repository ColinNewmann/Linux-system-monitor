#ifndef MONITOR_SYSTEM_INFO_H
#define MONITOR_SYSTEM_INFO_H
#include <stddef.h>
enum mon_command { CMD_INVALID, CMD_STATUS, CMD_CPU, CMD_MEM, CMD_UPTIME };
enum mon_command system_command(const char *request);
int system_response(enum mon_command command, char *response, size_t capacity);
#endif
