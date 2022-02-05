/* common/threads.h */

#pragma once

#include <memory>

#include "tbb/global_control.h"

extern int numthreads;

void LowerProcessPriority(void);
int GetDefaultThreads(void);
int GetMaxThreads(void); /* returns 0 if no limit specified */
int GetThreadWork(void);
int GetThreadWork_Locked__(void); /* caller must take care of locking */
void RunThreadsOn(int start, int workcnt, void *(func)(void *), void *arg);
void ThreadLock(void);
void ThreadUnlock(void);

/* Call if needing to print to stdout - should be called with lock held */
void InterruptThreadProgress__(void);

/**
 * Configures TBB to have the given max threads (specify 0 for unlimited).
 * 
 * Call this from main() and keep the returned object until main() finishes.
 */
std::unique_ptr<tbb::global_control> ConfigureTBB(int maxthreads);
