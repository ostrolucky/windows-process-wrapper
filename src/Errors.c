#include <windows.h>
#include <stdio.h>
#include "Errors.h"

static PSLIST_HEADER errors;

typedef struct _error_item {
    SLIST_ENTRY entry;
    char* message;
} error_item_t;

/*
* Polyfill for vasprintf()
* See https://stackoverflow.com/a/40160038/889949
*/
static int vasprintf(char **strp, const char *fmt, va_list ap)
{
    // _vscprintf tells you how big the buffer needs to be
    int len = _vscprintf(fmt, ap);
    if (len == -1) {
        return -1;
    }

    size_t size = (size_t)len + 1;
    char *str = (char *)malloc(size);

    if (!str) {
        return -1;
    }

    // _vsprintf_s is the "secure" version of vsprintf
    int r = vsprintf_s(str, len + 1, fmt, ap);

    if (r == -1) {
        free(str);
        return -1;
    }

    *strp = str;

    return r;
}

BOOL errors_init()
{
    if (errors != NULL) {
        return FALSE;
    }

    errors = _aligned_malloc(sizeof(SLIST_HEADER), MEMORY_ALLOCATION_ALIGNMENT);

    if (errors == NULL) {
        return FALSE;
    }

    InitializeSListHead(errors);

    return TRUE;
}

BOOL errors_destroy()
{
    if (errors == NULL) {
        return FALSE;
    }

    InterlockedFlushSList(errors);
    _aligned_free(errors);

    errors = NULL;

    return TRUE;
}

int errors_count()
{
    if (errors == NULL) {
        return -1;
    }

    return QueryDepthSList(errors);
}

BOOL error_push(char* format, ...)
{
    if (errors == NULL) {
        return FALSE;
    }

    error_item_t *item = _aligned_malloc(sizeof(error_item_t), MEMORY_ALLOCATION_ALIGNMENT);

    if (item == NULL) {
        return FALSE;
    }

    char *message;
    va_list ap;

    va_start(ap, format);
    int length = vasprintf(&message, format, ap);
    va_end(ap);

    /* trim whitespace from the end of the string */
    for (int i = length - 1; message[i] == ' ' || message[i] == '\t' || message[i] == '\r' || message[i] == '\n'; i--) {
        message[i] = 0;
    }

    item->message = message;
    InterlockedPushEntrySList(errors, &item->entry);

    return TRUE;
}

BOOL system_error_push(int code, char* message, ...)
{
    char *errstr = NULL;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&errstr, 0, NULL
        );

    if (message == NULL) {
        return error_push("%d: %s", code, errstr);
    }

    va_list ap;
    char *extra;

    va_start(ap, message);
    vasprintf(&extra, message, ap);
    va_end(ap);

    return error_push("%s: %d: %s", extra, code, errstr);
}

char *error_pop()
{
    if (errors == NULL) {
        return NULL;
    }

    error_item_t *item = (error_item_t*)InterlockedPopEntrySList(errors);

    if (item == NULL) {
        return NULL;
    }

    char *message = item->message;
    _aligned_free(item);

    return message;
}

/*
 * Output error messages to stderr and return -1
 */
RESULT errors_output_all()
{
    char *message;

    while (NULL != (message = error_pop())) {
        fprintf(stderr, "%s\n", message);
    }

    errors_destroy();

    return FAILURE;
}
