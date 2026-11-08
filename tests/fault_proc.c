#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode);

FILE *__wrap_fopen(const char *path, const char *mode)
{
    /* 僅測試用連結器替身，不更動真實 /proc 或正式程式。 */
    const char *failed_path = getenv("MONITOR_FAIL_PROC");
    if (failed_path && strcmp(failed_path, path) == 0) {
        errno = EACCES;
        return NULL;
    }
    return __real_fopen(path, mode);
}
