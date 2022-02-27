/* common/threads.h */

#pragma once

#include <memory>
#include <functional>

#include "tbb/global_control.h"

void GetThreadWork_Locked__(void); /* caller must take care of locking */
void RunThreadsOn(size_t start, size_t workcnt, std::function<void(size_t)> func);
void ThreadLock(void);
void ThreadUnlock(void);

/* Call if needing to print to stdout - should be called with lock held */
void InterruptThreadProgress__(void);

/**
 * Configures TBB to have the given max threads (specify 0 for unlimited).
 */
void configureTBB(int maxthreads);
