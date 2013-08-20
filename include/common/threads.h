/* common/threads.h */

#ifndef __COMMON_THREADS_H__
#define __COMMON_THREADS_H__

extern int numthreads;

int GetDefaultThreads(void);
int GetMaxThreads(void); /* returns 0 if no limit specified */
int GetThreadWork(void);
int GetThreadWork_Locked__(void); /* caller must take care of locking */
void RunThreadsOn(int start, int workcnt, void *(func)(void *), void *arg);
void ThreadLock(void);
void ThreadUnlock(void);

/* Call if needing to print to stdout - should be called with lock held */
void InterruptThreadProgress__(void);

#endif /* __COMMON_THREADS_H__ */
