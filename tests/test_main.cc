#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <common/log.hh>

int main(int argc, char** argv)
{
    logging::preinitialize();

    // writing console colors within test case output breaks doctest/CLion integration
    logging::enable_color_codes = false;

    doctest::Context context;

    context.applyCommandLine(argc, argv);
    int res = context.run();

    if (context.shouldExit()) {
        return res;
    }

    return res;
}
