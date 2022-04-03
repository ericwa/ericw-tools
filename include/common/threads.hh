/* common/threads.h */

#pragma once

#include <memory>
#include <functional>

#include "tbb/global_control.h"

/**
 * Configures TBB to have the given max threads (specify 0 for unlimited).
 */
void configureTBB(int maxthreads, bool lowPriority);
