#include <common/threads.hh>

#include <memory>
#include <common/log.hh>
#include "tbb/global_control.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static std::unique_ptr<tbb::global_control> tbbGlobalControl;

void configureTBB(int maxthreads, bool lowPriority)
{
    if (tbbGlobalControl) {
        logging::print("ignoring multiple configureTBB calls\n");
        // only allow calling once per process, so we can disable threading in test_main.cc
        // and further attempts to re-enable it will be ignored
        return;
    }

    tbbGlobalControl = std::unique_ptr<tbb::global_control>();

    if (maxthreads > 0) {
        tbbGlobalControl =
            std::make_unique<tbb::global_control>(tbb::global_control::max_allowed_parallelism, maxthreads);

        logging::print("running with {} thread(s)\n", maxthreads);
    }

    if (lowPriority) {
#ifdef _WIN32
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
        logging::print("running with lower priority\n");
#else
        logging::print("low priority not compiled into this version\n");
#endif
    }
}
