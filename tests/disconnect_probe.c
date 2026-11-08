#include "protocol.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    /* 使用真正的已關閉對端，確保 SIGPIPE 對照不依賴 TCP 時序。 */
    struct sigaction action = {.sa_handler = SIG_DFL};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, NULL) < 0) return 2;
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) < 0) { perror("socketpair"); return 2; }
    close(pair[1]);
    int result = mon_send_all(pair[0], "probe\n", 6, 1000);
    int saved = errno;
    close(pair[0]);
    printf("result=%d errno=%d EPIPE=%d\n", result, saved, EPIPE);
    return result == MON_IO_ERROR && saved == EPIPE ? 0 : 1;
}
