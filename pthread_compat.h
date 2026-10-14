#ifndef PTHREAD_COMPAT_H
#define PTHREAD_COMPAT_H

/*
 * pthread_compat.h
 * ----------------
 * Portability layer so the exact same benchmark source compiles everywhere:
 *
 *   - Linux / MSYS2 / MinGW-w64 (real POSIX threads): <pthread.h> is used
 *     as-is; compile with  -pthread  and link -lpthread as usual.
 *   - Old MinGW.org toolchains (no winpthreads): maps the small POSIX subset
 *     used by the benchmarks (pthread_create / pthread_join) onto the Win32
 *     API (CreateThread / WaitForSingleObject) with the same semantics:
 *     1 start routine (void* -> void*), 1 join that blocks until completion.
 */

#ifdef _WIN32

#include <windows.h>
#include <stdlib.h>

#ifndef PTHREAD_CREATE_JOINABLE
#define PTHREAD_CREATE_JOINABLE 0
#endif

typedef void *pthread_attr_t;

typedef struct pthread_compat_ctx {
    void *(*start_routine)(void *);
    void *arg;
    HANDLE handle;
} pthread_compat_ctx;

typedef pthread_compat_ctx *pthread_t;

/* Trampoline: adapts Win32 thread entry (DWORD WINAPI, LPVOID) to the
 * POSIX signature (void* (*)(void*)). Frees its argument block afterward. */
static DWORD WINAPI pthread_compat_trampoline(LPVOID p) {
    pthread_compat_ctx *ctx = (pthread_compat_ctx *)p;
    ctx->start_routine(ctx->arg);   /* return value is discarded, as unused here */
    return 0;
}

/* Returns 0 on success, nonzero on failure (like POSIX). */
static int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine)(void *), void *arg) {
    (void)attr;
    pthread_compat_ctx *ctx = (pthread_compat_ctx *)malloc(sizeof(pthread_compat_ctx));
    if (ctx == NULL) return 1;
    ctx->start_routine = start_routine;
    ctx->arg = arg;
    ctx->handle = CreateThread(NULL, 0, pthread_compat_trampoline, ctx, 0, NULL);
    if (ctx->handle == NULL) {
        free(ctx);
        return 1;
    }
    *thread = ctx;
    return 0;
}

/* Blocks until the thread exits, frees its context, returns 0. */
static int pthread_join(pthread_t thread, void **value_ptr) {
    if (thread == NULL) return 0;
    if (thread->handle != NULL) {
        WaitForSingleObject(thread->handle, INFINITE);
        CloseHandle(thread->handle);
    }
    if (value_ptr != NULL) *value_ptr = NULL;
    free(thread);
    return 0;
}

#else /* !defined(_WIN32): real POSIX threads */

#include <pthread.h>

#endif

#endif /* PTHREAD_COMPAT_H */
