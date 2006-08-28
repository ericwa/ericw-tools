/* common/threads.h */

#ifndef __COMMON_THREADS_H__
#define __COMMON_THREADS_H__

extern int numthreads;

int GetThreadWork(void);
void RunThreadsOn(int workcnt, qboolean showpacifier, void (*func) (int));
void ThreadLock(void);
void ThreadUnlock(void);

#endif /* __COMMON_THREADS_H__ */
