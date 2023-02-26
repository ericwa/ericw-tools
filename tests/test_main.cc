#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <common/log.hh>
#include <common/threads.hh>

int main(int argc, char **argv)
{
    logging::preinitialize();

    // writing console colors within test case output breaks doctest/CLion integration
    logging::enable_color_codes = false;

    // parse "-threads 1"
    for (int i = 1; i < argc - 1; ++i) {
        if (!strcmp("-threads", argv[i])) {
            configureTBB(atoi(argv[i + 1]), false);
            break;
        }
    }

    doctest::Context context;

    context.applyCommandLine(argc, argv);
    int res = context.run();

    if (context.shouldExit()) {
        return res;
    }

    return res;
}
