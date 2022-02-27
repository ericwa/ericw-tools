/* common/threads.h */

#pragma once

#include <memory>

#include "tbb/global_control.h"

void LowerProcessPriority(void);
int GetThreadWork(void);
int GetThreadWork_Locked__(void); /* caller must take care of locking */
void RunThreadsOn(int start, int workcnt, void *(func)(void *), void *arg);
void ThreadLock(void);
void ThreadUnlock(void);

/* Call if needing to print to stdout - should be called with lock held */
void InterruptThreadProgress__(void);

/**
 * Configures TBB to have the given max threads (specify 0 for unlimited).
 */
void configureTBB(int maxthreads);
