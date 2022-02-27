#include "gtest/gtest.h"
#include "common/settings.hh"

// test booleans
TEST(settings, booleanFlagImplicit)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(boolSetting.value(), true);
}

TEST(settings, booleanFlagExplicit)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "1"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(boolSetting.value(), true);
}

TEST(settings, booleanFlagStray)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "stray"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(boolSetting.value(), true);
}

// test scalars
TEST(settings, scalarSimple)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.25"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(scalarSetting.value(), 1.25);
}

TEST(settings, scalarNegative)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "-0.25"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(scalarSetting.value(), -0.25);
}

TEST(settings, scalarInfinity)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0, 0.0, std::numeric_limits<vec_t>::infinity());
    const char *arguments[] = {"qbsp.exe", "-scale", "INFINITY"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(scalarSetting.value(), std::numeric_limits<vec_t>::infinity());
}

TEST(settings, scalarNAN)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "NAN"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_TRUE(std::isnan(scalarSetting.value()));
}

TEST(settings, scalarScientific)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.54334E-34"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(scalarSetting.value(), 1.54334E-34);
}

TEST(settings, scalarEOF)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale"};
    ASSERT_THROW(settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1}), settings::parse_exception);
}

TEST(settings, scalarStray)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "stray"};
    ASSERT_THROW(settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1}), settings::parse_exception);
}

// test scalars
TEST(settings, vec3Simple)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "3"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(scalarSetting.value(), (qvec3d{1, 2, 3}));
}

TEST(settings, vec3Complex)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "-12.5", "-INFINITY", "NAN"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(scalarSetting.value()[0], -12.5);
    ASSERT_EQ(scalarSetting.value()[1], -std::numeric_limits<vec_t>::infinity());
    ASSERT_TRUE(std::isnan(scalarSetting.value()[2]));
}

TEST(settings, vec3Incomplete)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2"};
    ASSERT_THROW(settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1}), settings::parse_exception);
}

TEST(settings, vec3Stray)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "abc"};
    ASSERT_THROW(settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1}), settings::parse_exception);
}

// test string formatting
TEST(settings, stringSimple)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "-name", "i am a string with spaces in it"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(stringSetting.value(), arguments[2]);
}

TEST(settings, stringSpan)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "-name", "i", "am", "a", "string"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(stringSetting.value(), "i am a string");
}

TEST(settings, stringSpanWithBlockingOption)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    settings::setting_bool flagSetting(&settings, "flag", false);
    const char *arguments[] = {"qbsp.exe", "-name", "i", "am", "a", "string", "-flag"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(stringSetting.value(), "i am a string");
    ASSERT_EQ(flagSetting.value(), true);
}

// test remainder
TEST(settings, remainder)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    settings::setting_bool flagSetting(&settings, "flag", false);
    const char *arguments[] = {
        "qbsp.exe", "-name", "i", "am", "a", "string", "-flag", "remainder one", "remainder two"};
    auto remainder = settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(remainder[0], "remainder one");
    ASSERT_EQ(remainder[1], "remainder two");
}

// test double-hyphens
TEST(settings, doubleHyphen)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "--locked", "--name", "my name!"};
    settings.parse(token_parser_t{std::size(arguments) - 1, arguments + 1});
    ASSERT_EQ(boolSetting.value(), true);
    ASSERT_EQ(stringSetting.value(), "my name!");
}

// test groups; ensure that performance is the first group
TEST(settings, grouping)
{
    settings::setting_container settings;
    settings::setting_group performance{"Performance", -1000};
    settings::setting_group others{"Others", 1000};
    settings::setting_scalar scalarSetting(
        &settings, "threads", 0, &performance, "number of threads; zero for automatic");
    settings::setting_bool boolSetting(
        &settings, "fast", false, &performance, "use faster algorithm, for quick compiles");
    settings::setting_string stringSetting(
        &settings, "filename", "filename.bat", "file.bat", &others, "some batch file");
    ASSERT_TRUE(settings.grouped().begin()->first == &performance);
    // settings.printHelp();
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
