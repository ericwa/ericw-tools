#include "test_main.hh"

#include <gtest/gtest.h>

#include <common/log.hh>
#include <common/threads.hh>
#include <common/fs.hh>
#include <common/imglib.hh>

bool tests_verbose = false;

class clear_shared_data_listener : public testing::TestEventListener
{
public:
    void OnTestProgramStart(const testing::UnitTest &unit_test) override { }
    void OnTestIterationStart(const testing::UnitTest &unit_test, int iteration) override { }
    void OnEnvironmentsSetUpStart(const testing::UnitTest &unit_test) override { }
    void OnEnvironmentsSetUpEnd(const testing::UnitTest &unit_test) override { }
    void OnTestSuiteStart(const testing::TestSuite &test_suite) override { }
    void OnTestStart(const testing::TestInfo &test_info) override {
    fs::clear();
    img::clear();
    }
    void OnTestDisabled(const testing::TestInfo &test_info) override { }
    void OnTestPartResult(const testing::TestPartResult &test_part_result) override { }
    void OnTestEnd(const testing::TestInfo &test_info) override { }
    void OnTestSuiteEnd(const testing::TestSuite &test_suite) override { }
    void OnEnvironmentsTearDownStart(const testing::UnitTest &unit_test) override { }
    void OnEnvironmentsTearDownEnd(const testing::UnitTest &unit_test) override { }
    void OnTestIterationEnd(const testing::UnitTest &unit_test, int iteration) override { }
    void OnTestProgramEnd(const testing::UnitTest &unit_test) override { }
};

int main(int argc, char **argv)
{
    logging::preinitialize();

    // writing console colors within test case output breaks doctest/CLion integration
    logging::enable_color_codes = false;

    for (int i = 1; i < argc; ++i) {
        // parse "-threads 1"
        if (!strcmp("-threads", argv[i]) || !strcmp("--threads", argv[i])) {
            if (!(i + 1 < argc)) {
                logging::print("--threads requires an argument\n");
                exit(1);
            }
            configureTBB(atoi(argv[i + 1]), false);
            continue;
        }
        // parse "-verbose"
        if (!strcmp("-verbose", argv[i]) || !strcmp("--verbose", argv[i])) {
            tests_verbose = true;
            continue;
        }
    }

    testing::InitGoogleTest(&argc, argv);

    // clear fs, etc., between each test
    testing::TestEventListeners &listeners = testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new clear_shared_data_listener());

    return RUN_ALL_TESTS();
}
