/* common/threads.h */

#ifndef __COMMON_THREADS_H__
#define __COMMON_THREADS_H__

#include <common/cmdlib.h>

extern int numthreads;

int GetDefaultThreads(void);
int GetMaxThreads(void); /* returns 0 if no limit specified */
int GetThreadWork(void);
int GetThreadWork_Locked__(void); /* caller must take care of locking */
void RunThreadsOn(int workcnt, void *(func)(void *));
void ThreadLock(void);
void ThreadUnlock(void);

#endif /* __COMMON_THREADS_H__ */
