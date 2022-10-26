/* common/threads.h */

#pragma once

/**
 * Configures TBB to have the given max threads (specify 0 for unlimited).
 */
void configureTBB(int maxthreads, bool lowPriority);
