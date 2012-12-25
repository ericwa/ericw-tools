/* common/threads.h */

#ifndef __COMMON_THREADS_H__
#define __COMMON_THREADS_H__

extern int numthreads;

int GetDefaultThreads(void);
int GetMaxThreads(void); /* returns 0 if no limit specified */
int GetThreadWork(void);
void RunThreadsOn(int workcnt, qboolean showpacifier, void *(func)(void *));
void ThreadLock(void);
void ThreadUnlock(void);

#endif /* __COMMON_THREADS_H__ */
