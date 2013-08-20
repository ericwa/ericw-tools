/* common/threads.c */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <common/log.h>
#include <common/threads.h>

/*
 * FIXME - Temporary hack while trying to get qbsp to use the common
 *         thread/logging code.  Error() would normally be defined in
 *         either common/cmdlib.h or qbsp/qbsp.h.
 */
void Error(const char *error, ...)
    __attribute__((format(printf,1,2),noreturn));

/* Make the locks no-ops if we aren't running threads */
static _Bool threads_active = false;

static int dispatch;
static int workcount;
static int oldpercent = -1;

/*
 * =============
 * GetThreadWork
 * =============
 */
int
GetThreadWork_Locked__(void)
{
    int ret;
    int percent;

    if (dispatch == workcount)
	return -1;

    percent = 50 * dispatch / workcount;
    while (oldpercent < percent) {
	oldpercent++;
	logprint_locked__("%c", (oldpercent % 5) ? '.' : '0' + (oldpercent / 5));
    }

    ret = dispatch;
    dispatch++;

    return ret;
}

int
GetThreadWork(void)
{
    int ret;

    ThreadLock();
    ret = GetThreadWork_Locked__();
    ThreadUnlock();

    return ret;
}

void
InterruptThreadProgress__(void)
{
    if (oldpercent != -1) {
	logprint_locked__("\\\n");
	oldpercent = -1;
    }
}

/*
 * ===================================================================
 *                              WIN32
 * ===================================================================
 */
#ifdef WIN32
#define HAVE_THREADS

#include <windows.h>

int numthreads = 1;
CRITICAL_SECTION crit;

int
GetDefaultThreads(void)
{
    SYSTEM_INFO info;

    GetSystemInfo(&info);

    return info.dwNumberOfProcessors;
}

void
ThreadLock(void)
{
    if (threads_active)
	EnterCriticalSection(&crit);
}

void
ThreadUnlock(void)
{
    if (threads_active)
	LeaveCriticalSection(&crit);
}

/*
 * =============
 * RunThreadsOn
 * =============
 */
void
RunThreadsOn(int start, int workcnt, void *(func)(void *), void *arg)
{
    uintptr_t i; /* avoid warning due to cast for the CreateThread API */
    DWORD *threadid;
    HANDLE *threadhandle;

    dispatch = start;
    workcount = workcnt;
    oldpercent = -1;

    threadid = malloc(sizeof(*threadid) * numthreads);
    threadhandle = malloc(sizeof(*threadhandle) * numthreads);

    if (!threadid || !threadhandle)
	Error("Failed to allocate memory for threads");

    /* run threads in parallel */
    InitializeCriticalSection(&crit);
    threads_active = true;
    for (i = 0; i < numthreads; i++) {
	threadhandle[i] = CreateThread(NULL,
				       0,
				       (LPTHREAD_START_ROUTINE)func,
				       (LPVOID)arg,
				       0,
				       &threadid[i]);
    }

    for (i = 0; i < numthreads; i++)
	WaitForSingleObject(threadhandle[i], INFINITE);

    threads_active = false;
    oldpercent = -1;
    DeleteCriticalSection(&crit);

    logprint("\n");

    free(threadhandle);
    free(threadid);
}

#endif /* WIN32 */

/*
 * ===================================================================
 *                               PTHREADS
 * ===================================================================
 */

#ifdef USE_PTHREADS
#define HAVE_THREADS

#include <pthread.h>
#include <unistd.h>

int numthreads = 1;
pthread_mutex_t *my_mutex;

int
GetDefaultThreads(void)
{
    int threads;

#ifdef _SC_NPROCESSORS_ONLN
    threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (threads < 1)
	threads = 1;
#else
    threads = 4;
#endif

    return threads;
}

void
ThreadLock(void)
{
    if (threads_active)
	pthread_mutex_lock(my_mutex);
}

void
ThreadUnlock(void)
{
    if (threads_active)
	pthread_mutex_unlock(my_mutex);
}


/*
 * =============
 * RunThreadsOn
 * =============
 */
void
RunThreadsOn(int start, int workcnt, void *(func)(void *), void *arg)
{
    pthread_t *threads;
    pthread_mutexattr_t mattrib;
    pthread_attr_t attrib;
    int status;
    int i;

    dispatch = start;
    workcount = workcnt;
    oldpercent = -1;

    status = pthread_mutexattr_init(&mattrib);
    if (status)
	Error("pthread_mutexattr_init failed");

    my_mutex = malloc(sizeof(*my_mutex));
    if (!my_mutex)
	Error("failed to allocate memory for thread mutex");
    status = pthread_mutex_init(my_mutex, &mattrib);
    if (status)
	Error("pthread_mutex_init failed");

    status = pthread_attr_init(&attrib);
    if (status)
	Error("pthread_attr_init failed");
#if 0
    status = pthread_attr_setstacksize(&attrib, 0x100000);
    if (status)
	Error("pthread_attr_setstacksize failed");
#endif

    threads = malloc(sizeof(*threads) * numthreads);
    if (!threads)
	Error("failed to allocate memory for threads");

    threads_active = true;

    for (i = 0; i < numthreads; i++) {
	status = pthread_create(&threads[i], &attrib, func, arg);
	if (status)
	    Error("pthread_create failed");
    }

    for (i = 0; i < numthreads; i++) {
	status = pthread_join(threads[i], NULL);
	if (status)
	    Error("pthread_join failed");
    }

    threads_active = false;
    oldpercent = -1;

    status = pthread_mutex_destroy(my_mutex);
    if (status)
	Error("pthread_mutex_destroy failed");

    free(threads);
    free(my_mutex);

    logprint("\n");
}

#endif /* USE_PTHREADS */

/*
 * =======================================================================
 *                                SINGLE THREAD
 * =======================================================================
 */

#ifndef HAVE_THREADS

int numthreads = 1;

void ThreadLock(void) {}
void ThreadUnlock(void) {}

/*
 * =============
 * RunThreadsOn
 * =============
 */
void
RunThreadsOn(int start, int workcnt, void *(func)(void *), void *arg)
{
    dispatch = start;
    workcount = workcnt;
    oldpercent = -1;

    func(arg);

    logprint("\n");
}

#endif
