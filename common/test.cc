#include "gtest/gtest.h"
#include "common/settings.hh"

#include <type_traits>

// test booleans
TEST(settings, booleanFlagImplicit)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(boolSetting.value() == true);
}

TEST(settings, booleanFlagExplicit)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "1"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(boolSetting.value() == true);
}

TEST(settings, booleanFlagStray)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "stray"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(boolSetting.value() == true);
}

// test scalars
TEST(settings, scalarSimple)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(scalarSetting.value() == 1.25);
}

TEST(settings, scalarNegative)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "-0.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(scalarSetting.value() == -0.25);
}

TEST(settings, scalarInfinity)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0, 0.0, std::numeric_limits<vec_t>::infinity());
    const char *arguments[] = {"qbsp.exe", "-scale", "INFINITY"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(scalarSetting.value() == std::numeric_limits<vec_t>::infinity());
}

TEST(settings, scalarNAN)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "NAN"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(std::isnan(scalarSetting.value()));
}

TEST(settings, scalarScientific)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.54334E-34"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(scalarSetting.value() == 1.54334E-34);
}

TEST(settings, scalarEOF)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

TEST(settings, scalarStray)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "stray"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

// test scalars
TEST(settings, vec3Simple)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "3"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(scalarSetting.value() == (qvec3d{1, 2, 3}));
}

TEST(settings, vec3Complex)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "-12.5", "-INFINITY", "NAN"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(scalarSetting.value()[0] == -12.5);
    ASSERT_TRUE(scalarSetting.value()[1] == -std::numeric_limits<vec_t>::infinity());
    ASSERT_TRUE(std::isnan(scalarSetting.value()[2]));
}

TEST(settings, vec3Incomplete)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

TEST(settings, vec3Stray)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "abc"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

// test string formatting
TEST(settings, stringSimple)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "-name", "i am a string with spaces in it"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(stringSetting.value() == arguments[2]);
}

TEST(settings, stringSpan)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "-name", "i", "am", "a", "string"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(stringSetting.value() == "i am a string");
}

TEST(settings, stringSpanWithBlockingOption)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    settings::setting_bool flagSetting(&settings, "flag", false);
    const char *arguments[] = {"qbsp.exe", "-name", "i", "am", "a", "string", "-flag"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(stringSetting.value() == "i am a string");
    ASSERT_TRUE(flagSetting.value() == true);
}

// test remainder
TEST(settings, remainder)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    settings::setting_bool flagSetting(&settings, "flag", false);
    const char *arguments[] = {
        "qbsp.exe", "-name", "i", "am", "a", "string", "-flag", "remainder one", "remainder two"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    auto remainder = settings.parse(p);
    ASSERT_TRUE(remainder[0] == "remainder one");
    ASSERT_TRUE(remainder[1] == "remainder two");
}

// test double-hyphens
TEST(settings, doubleHyphen)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "--locked", "--name", "my name!"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1};
    settings.parse(p);
    ASSERT_TRUE(boolSetting.value() == true);
    ASSERT_TRUE(stringSetting.value() == "my name!");
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

TEST(settings, copy)
{
    settings::setting_container settings;
    settings::setting_scalar scaleSetting(&settings, "scale", 1.5);
    settings::setting_scalar waitSetting(&settings, "wait", 0.0);
    settings::setting_string stringSetting(&settings, "string", "test");

    EXPECT_TRUE(settings::source::DEFAULT == scaleSetting.getSource());
    EXPECT_TRUE(settings::source::DEFAULT == waitSetting.getSource());
    EXPECT_TRUE(0 == waitSetting.value());

    EXPECT_TRUE(waitSetting.copyFrom(scaleSetting));
    EXPECT_TRUE(settings::source::DEFAULT == waitSetting.getSource());
    EXPECT_TRUE(1.5 == waitSetting.value());

    // if copy fails, the value remains unchanged
    EXPECT_TRUE(!waitSetting.copyFrom(stringSetting));
    EXPECT_TRUE(settings::source::DEFAULT == waitSetting.getSource());
    EXPECT_TRUE(1.5 == waitSetting.value());

    scaleSetting.setValue(2.5);
    EXPECT_TRUE(settings::source::MAP == scaleSetting.getSource());

    // source is also copied
    EXPECT_TRUE(waitSetting.copyFrom(scaleSetting));
    EXPECT_TRUE(settings::source::MAP == waitSetting.getSource());
    EXPECT_TRUE(2.5 == waitSetting.value());
}

TEST(settings, copyMangle)
{
    settings::setting_container settings;
    settings::setting_mangle sunvec{&settings, {"sunlight_mangle"}, 0.0, 0.0, 0.0};

    parser_t p(std::string_view("0.0 -90.0 0.0"));
    EXPECT_TRUE(sunvec.parse("", p));
    EXPECT_NEAR(0, sunvec.value()[0], 1e-6);
    EXPECT_NEAR(0, sunvec.value()[1], 1e-6);
    EXPECT_NEAR(-1, sunvec.value()[2], 1e-6);

    settings::setting_mangle sunvec2{&settings, {"sunlight_mangle2"}, 0.0, 0.0, 0.0};
    sunvec2.copyFrom(sunvec);

    EXPECT_NEAR(0, sunvec2.value()[0], 1e-6);
    EXPECT_NEAR(0, sunvec2.value()[1], 1e-6);
    EXPECT_NEAR(-1, sunvec2.value()[2], 1e-6);
}

TEST(settings, copyContainer)
{
    settings::setting_container settings1;
    settings::setting_bool boolSetting1(&settings1, "boolSetting", false);
    EXPECT_TRUE(!boolSetting1.value());
    EXPECT_TRUE(settings::source::DEFAULT == boolSetting1.getSource());

    boolSetting1.setValue(true);
    EXPECT_TRUE(boolSetting1.value());
    EXPECT_TRUE(settings::source::MAP == boolSetting1.getSource());

    {
        settings::setting_container settings2;
        settings::setting_bool boolSetting2(&settings2, "boolSetting", false);
        EXPECT_TRUE(!boolSetting2.value());

        settings2.copyFrom(settings1);
        EXPECT_TRUE(boolSetting2.value());
        EXPECT_TRUE(settings::source::MAP == boolSetting2.getSource());
    }
}

const settings::setting_group test_group{"Test"};

TEST(settings, copyContainerSubclass)
{
    struct my_settings : public settings::setting_container {
        settings::setting_bool boolSetting {this, "boolSetting", false, &test_group};
        settings::setting_string stringSetting {this, "stringSetting", "default", "\"str\"", &test_group};
    };

    static_assert(!std::is_copy_constructible_v<settings::setting_container>);
    static_assert(!std::is_copy_constructible_v<settings::setting_bool>);
    static_assert(!std::is_copy_constructible_v<my_settings>);

    my_settings s1;
    EXPECT_TRUE(&s1.boolSetting == s1.findSetting("boolSetting"));
    EXPECT_TRUE(&s1.stringSetting == s1.findSetting("stringSetting"));
    EXPECT_TRUE(1 == s1.grouped().size());
    EXPECT_TRUE((std::set<settings::setting_base *>{ &s1.boolSetting, &s1.stringSetting }) == s1.grouped().at(&test_group));
    s1.boolSetting.setValue(true);
    EXPECT_TRUE(settings::source::MAP == s1.boolSetting.getSource());

    my_settings s2;
    s2.copyFrom(s1);
    EXPECT_TRUE(&s2.boolSetting == s2.findSetting("boolSetting"));
    EXPECT_TRUE(s2.grouped().size() == 1);
    EXPECT_TRUE((std::set<settings::setting_base *>{ &s2.boolSetting, &s2.stringSetting }) == s2.grouped().at(&test_group));
    EXPECT_TRUE(s2.boolSetting.value());
    EXPECT_TRUE(settings::source::MAP == s2.boolSetting.getSource());

    // s2.stringSetting is still at its default
    EXPECT_TRUE("default" == s2.stringSetting.value());
    EXPECT_TRUE(settings::source::DEFAULT == s2.stringSetting.getSource());
}

TEST(settings, resetBool)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting1(&settings, "boolSetting", false);

    boolSetting1.setValue(true);
    EXPECT_TRUE(settings::source::MAP == boolSetting1.getSource());
    EXPECT_TRUE(boolSetting1.value());

    boolSetting1.reset();
    EXPECT_TRUE(settings::source::DEFAULT == boolSetting1.getSource());
    EXPECT_TRUE(!boolSetting1.value());
}

TEST(settings, resetScalar)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting1(&settings, "scalarSetting", 12.34);

    scalarSetting1.setValue(-2);
    EXPECT_TRUE(settings::source::MAP == scalarSetting1.getSource());
    EXPECT_TRUE(-2 == scalarSetting1.value());

    scalarSetting1.reset();
    EXPECT_TRUE(settings::source::DEFAULT == scalarSetting1.getSource());
    EXPECT_TRUE(12.34 == scalarSetting1.value());
}

TEST(settings, resetContainer)
{
    settings::setting_container settings;
    settings::setting_vec3 vec3Setting1(&settings, "vec", 3, 4, 5);
    settings::setting_string stringSetting1(&settings, "name", "abc");

    vec3Setting1.setValue(qvec3d(-1, -2, -3));
    stringSetting1.setValue("test");
    settings.reset();

    EXPECT_TRUE(settings::source::DEFAULT == vec3Setting1.getSource());
    EXPECT_TRUE(qvec3d(3, 4, 5) == vec3Setting1.value());

    EXPECT_TRUE(settings::source::DEFAULT == stringSetting1.getSource());
    EXPECT_TRUE("abc" == stringSetting1.value());
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
