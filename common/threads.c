/* common/threads.c */

#include <common/cmdlib.h>
#include <common/log.h>
#include <common/threads.h>

static int dispatch;
static int workcount;
static int oldpercent;
static qboolean pacifier;

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
    if (percent != oldpercent) {
	oldpercent = percent;
	if (pacifier)
	    logprint("%c", (percent % 5) ? '.' : '0' + (percent / 5));
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
    EnterCriticalSection(&crit);
}

void
ThreadUnlock(void)
{
    LeaveCriticalSection(&crit);
}

/*
 * =============
 * RunThreadsOn
 * =============
 */
void
RunThreadsOn(int workcnt, qboolean showpacifier, void *(func)(void *))
{
    int i;
    DWORD *threadid;
    HANDLE *threadhandle;

    dispatch = 0;
    workcount = workcnt;
    oldpercent = -1;
    pacifier = showpacifier;

    threadid = malloc(sizeof(*threadid) * numthreads);
    threadhandle = malloc(sizeof(*threadhandle) * numthreads);

    if (!threadid || !threadhandle)
	Error("Failed to allocate memory for threads");

    /* run threads in parallel */
    InitializeCriticalSection(&crit);
    for (i = 0; i < numthreads; i++) {
	threadhandle[i] = CreateThread(NULL,
				       0,
				       (LPTHREAD_START_ROUTINE)func,
				       (LPVOID)i,
				       0,
				       &threadid[i]);
    }

    for (i = 0; i < numthreads; i++)
	WaitForSingleObject(threadhandle[i], INFINITE);
    DeleteCriticalSection(&crit);
    if (pacifier)
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
    pthread_mutex_lock(my_mutex);
}

void
ThreadUnlock(void)
{
    pthread_mutex_unlock(my_mutex);
}


/*
 * =============
 * RunThreadsOn
 * =============
 */
void
RunThreadsOn(int workcnt, qboolean showpacifier, void *(func)(void *))
{
    pthread_t *threads;
    pthread_mutexattr_t mattrib;
    pthread_attr_t attrib;
    int status;
    int i;

    dispatch = 0;
    workcount = workcnt;
    oldpercent = -1;
    pacifier = showpacifier;

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

    for (i = 0; i < numthreads; i++) {
	status = pthread_create(&threads[i], &attrib, func, NULL);
	if (status)
	    Error("pthread_create failed");
    }

    for (i = 0; i < numthreads; i++) {
	status = pthread_join(threads[i], NULL);
	if (status)
	    Error("pthread_join failed");
    }

    status = pthread_mutex_destroy(my_mutex);
    if (status)
	Error("pthread_mutex_destroy failed");

    free(threads);
    free(my_mutex);

    if (pacifier)
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
RunThreadsOn(int workcnt, qboolean showpacifier, void *(func)(void *))
{
    dispatch = 0;
    workcount = workcnt;
    oldpercent = -1;
    pacifier = showpacifier;

    func(0);

    if (pacifier)
	logprint("\n");
}

#endif
