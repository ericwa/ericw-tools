/* common/threads.c */

#include <cstdint>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <vector>
#include <optional>

#include <common/log.hh>
#include <common/threads.hh>

/* Make the locks no-ops if we aren't running threads */
static bool threads_active = false;

static size_t dispatch;
static size_t workcount;
static std::optional<size_t> oldpercent = std::nullopt;

/*
 * =============
 * GetThreadWork
 * =============
 */
void GetThreadWork_Locked__(void)
{
    if (dispatch == workcount)
        return;

    size_t percent = 50 * dispatch / workcount;

    while (!oldpercent.has_value() || oldpercent.value() < percent) {
        if (!oldpercent.has_value()) {
            oldpercent = 0;
        } else {
            (*oldpercent)++;
        }
        LogPrintLocked("{:c}", (oldpercent.value() % 5) ? '.' : '0' + (oldpercent.value() / 5));
    }

    dispatch++;
}

void InterruptThreadProgress__(void)
{
    if (oldpercent.has_value()) {
        LogPrintLocked("\\\n");
        oldpercent.reset();
    }
}

std::mutex crit;

void ThreadLock()
{
    if (threads_active)
        crit.lock();
}

void ThreadUnlock()
{
    if (threads_active)
        crit.unlock();
}

#include <tbb/parallel_for.h>

/*
 * =============
 * RunThreadsOn
 * =============
 */
void RunThreadsOn(size_t start, size_t workcnt, std::function<void(size_t)> func)
{
    std::vector<std::thread> threadhandle;

    dispatch = start;
    workcount = workcnt;
    oldpercent.reset();

    /* run threads in parallel */
    threads_active = true;

    tbb::parallel_for(start, workcnt, [func](size_t i) {
        ThreadLock();
        GetThreadWork_Locked__();
        ThreadUnlock();
    
        func(i);
    });

    threads_active = false;
    oldpercent.reset();

    LogPrint("\n");
}


/*
 * =======================================================================
 *                                TBB
 * =======================================================================
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

static std::unique_ptr<tbb::global_control> tbbGlobalControl;

void configureTBB(int maxthreads, bool lowPriority)
{
    tbbGlobalControl = std::unique_ptr<tbb::global_control>();

    if (maxthreads > 0) {
        tbbGlobalControl =
            std::make_unique<tbb::global_control>(tbb::global_control::max_allowed_parallelism, maxthreads);

        LogPrint("running with {} thread(s)\n", maxthreads);
    }

    if (lowPriority) {
#ifdef _WIN32
        SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
        LogPrint("running with lower priority\n");
#else
        LogPrint("low priority not compiled into this version\n");
#endif
    }
}
