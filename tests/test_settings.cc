#include <gtest/gtest.h>

#include "common/settings.hh"

#include <type_traits>

// test booleans
TEST(settings, booleanFlagImplicit)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(boolSetting.value(), true);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, booleanFlagExplicit)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "1"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(boolSetting.value(), true);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, booleanFlagStray)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "stray"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(boolSetting.value(), true);
    ASSERT_EQ(remainder, (std::vector<std::string>{"stray"}));
}

// test scalars
TEST(settings, scalarSimple)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(scalarSetting.value(), 1.25f);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, scalarNegative)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "-0.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(scalarSetting.value(), -0.25f);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, scalarInfinity)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0, 0.0, std::numeric_limits<double>::infinity());
    const char *arguments[] = {"qbsp.exe", "-scale", "INFINITY"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(scalarSetting.value(), std::numeric_limits<float>::infinity());
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, scalarNAN)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "NAN"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_TRUE(std::isnan(scalarSetting.value()));
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, scalarScientific)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.54334E-34"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(scalarSetting.value(), 1.54334E-34f);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, scalarEOF)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

TEST(settings, scalarStray)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "stray"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

// test int32 with implicit default
TEST(settings, int32CanOmitArgumentDefault)
{
    settings::setting_container settings;
    settings::setting_int32 setting(&settings, "bounce", 0, 0, 100, settings::can_omit_argument_tag(), 1);
    ASSERT_EQ(setting.value(), 0);
}

TEST(settings, int32CanOmitArgumentSimple)
{
    settings::setting_container settings;
    settings::setting_int32 setting(&settings, "bounce", 0, 0, 100, settings::can_omit_argument_tag(), 1);
    const char *arguments[] = {"qbsp.exe", "-bounce", "2"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(setting.value(), 2);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, int32CanOmitArgumentWithFollingSetting)
{
    settings::setting_container settings;
    settings::setting_int32 setting(&settings, "bounce", 0, 0, 100, settings::can_omit_argument_tag(), 1);
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-bounce", "-scale", "0.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(setting.value(), 1);
    ASSERT_EQ(scalarSetting.value(), 0.25);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, int32CanOmitArgumentEOF)
{
    settings::setting_container settings;
    settings::setting_int32 setting(&settings, "bounce", 0, 0, 100, settings::can_omit_argument_tag(), 1);
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-bounce"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(setting.value(), 1);
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, int32CanOmitArgumentRemainder)
{
    settings::setting_container settings;
    settings::setting_int32 setting(&settings, "bounce", 0, 0, 100, settings::can_omit_argument_tag(), 1);
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-bounce", "remainder"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(remainder, std::vector<std::string>{"remainder"});
}

// test enum

enum class testenum
{
    A = 0,
    B = 1,
    C = 2,
    D = 3
};

class SettingEnumTest : public testing::Test
{
protected:
    settings::setting_container settings;

    settings::setting_enum<testenum> enum_required_arg{&settings, "enum_required_arg", testenum::A,
        {{"A", testenum::A}, {"B", testenum::B}, {"C", testenum::C}, {"D", testenum::D}}};

    // no arg specified gives A.
    // -enum_optional_arg alone is an alias for B.
    settings::setting_enum<testenum> enum_optional_arg{&settings, "enum_optional_arg", testenum::A,
        {{"A", testenum::A}, {"B", testenum::B}, {"C", testenum::C}, {"D", testenum::D}},
        settings::can_omit_argument_tag(), testenum::B};

    settings::setting_scalar scalar_setting{&settings, "scale", 1.0};
};

TEST_F(SettingEnumTest, enumRequiredArgMissing)
{
    ASSERT_THROW(settings.parse_string("-enum_required_arg -scale 3"), settings::parse_exception);
    ASSERT_EQ(scalar_setting.value(), 1);
}

TEST_F(SettingEnumTest, enumRequired)
{
    ASSERT_EQ(settings.parse_string("-enum_required_arg C -scale 3"), std::vector<std::string>());
    ASSERT_EQ(enum_required_arg.value(), testenum::C);
    ASSERT_EQ(enum_optional_arg.value(), testenum::A);
    ASSERT_EQ(scalar_setting.value(), 3);
}

TEST_F(SettingEnumTest, enumOptional)
{
    ASSERT_EQ(settings.parse_string("-enum_optional_arg D remainder"), (std::vector<std::string>{"remainder"}));
    ASSERT_EQ(enum_optional_arg.value(), testenum::D);
}

TEST_F(SettingEnumTest, enumOptionalOmittedEOF)
{
    ASSERT_EQ(settings.parse_string("-enum_optional_arg"), std::vector<std::string>());
    ASSERT_EQ(enum_optional_arg.value(), testenum::B);
}

TEST_F(SettingEnumTest, enumOptionalOmittedWithNextArg)
{
    ASSERT_EQ(settings.parse_string("-enum_optional_arg -scale 3"), std::vector<std::string>());
    ASSERT_EQ(enum_optional_arg.value(), testenum::B);
    ASSERT_EQ(scalar_setting.value(), 3);
}

TEST_F(SettingEnumTest, enumOptionalOmittedWithRemainder)
{
    ASSERT_EQ(settings.parse_string("-enum_optional_arg remainder"), (std::vector<std::string>{"remainder"}));
    ASSERT_EQ(enum_optional_arg.value(), testenum::B);
    ASSERT_EQ(scalar_setting.value(), 1);
}

// test vec3
TEST(settings, vec3Simple)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "3"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(scalarSetting.value(), (qvec3f{1, 2, 3}));
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, vec3Complex)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "-12.5", "-INFINITY", "NAN"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(scalarSetting.value()[0], -12.5f);
    ASSERT_EQ(scalarSetting.value()[1], -std::numeric_limits<float>::infinity());
    ASSERT_TRUE(std::isnan(scalarSetting.value()[2]));
    ASSERT_TRUE(remainder.empty());
}

TEST(settings, vec3Incomplete)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

TEST(settings, vec3Stray)
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "abc"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    ASSERT_THROW(settings.parse(p), settings::parse_exception);
}

// test string formatting
TEST(settings, stringSimple)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "-name", "i am a string with spaces in it"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(stringSetting.value(), arguments[2]);
    ASSERT_TRUE(remainder.empty());
}

// test remainder
TEST(settings, remainder)
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    settings::setting_bool flagSetting(&settings, "flag", false);
    const char *arguments[] = {"qbsp.exe", "-name", "string", "-flag", "remainder one", "remainder two"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
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
    token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
    auto remainder = settings.parse(p);
    ASSERT_EQ(boolSetting.value(), true);
    ASSERT_EQ(stringSetting.value(), "my name!");
    ASSERT_TRUE(remainder.empty());
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
    ASSERT_EQ(settings.grouped().begin()->first, &performance);
    // settings.printHelp();
}

TEST(settings, copy)
{
    settings::setting_container settings;
    settings::setting_scalar scaleSetting(&settings, "scale", 1.5);
    settings::setting_scalar waitSetting(&settings, "wait", 0.0);
    settings::setting_string stringSetting(&settings, "string", "test");

    EXPECT_EQ(settings::source::DEFAULT, scaleSetting.get_source());
    EXPECT_EQ(settings::source::DEFAULT, waitSetting.get_source());
    EXPECT_EQ(0, waitSetting.value());

    EXPECT_TRUE(waitSetting.copy_from(scaleSetting));
    EXPECT_EQ(settings::source::DEFAULT, waitSetting.get_source());
    EXPECT_EQ(1.5, waitSetting.value());

    // if copy fails, the value remains unchanged
    EXPECT_FALSE(waitSetting.copy_from(stringSetting));
    EXPECT_EQ(settings::source::DEFAULT, waitSetting.get_source());
    EXPECT_EQ(1.5, waitSetting.value());

    scaleSetting.set_value(2.5, settings::source::MAP);
    EXPECT_EQ(settings::source::MAP, scaleSetting.get_source());

    // source is also copied
    EXPECT_TRUE(waitSetting.copy_from(scaleSetting));
    EXPECT_EQ(settings::source::MAP, waitSetting.get_source());
    EXPECT_EQ(2.5, waitSetting.value());
}

TEST(settings, copyMangle)
{
    settings::setting_container settings;
    settings::setting_mangle sunvec{&settings, {"sunlight_mangle"}, 0.0, 0.0, 0.0};

    parser_t p(std::string_view("0.0 -90.0 0.0"), {});
    EXPECT_TRUE(sunvec.parse("", p, settings::source::COMMANDLINE));
    EXPECT_NEAR(0, sunvec.value()[0], 1e-7);
    EXPECT_NEAR(0, sunvec.value()[1], 1e-7);
    EXPECT_NEAR(-1, sunvec.value()[2], 1e-7);

    settings::setting_mangle sunvec2{&settings, {"sunlight_mangle2"}, 0.0, 0.0, 0.0};
    sunvec2.copy_from(sunvec);

    EXPECT_NEAR(0, sunvec2.value()[0], 1e-7);
    EXPECT_NEAR(0, sunvec2.value()[1], 1e-7);
    EXPECT_NEAR(-1, sunvec2.value()[2], 1e-7);
}

TEST(settings, copyContainer)
{
    settings::setting_container settings1;
    settings::setting_bool boolSetting1(&settings1, "boolSetting", false);
    EXPECT_FALSE(boolSetting1.value());
    EXPECT_EQ(settings::source::DEFAULT, boolSetting1.get_source());

    boolSetting1.set_value(true, settings::source::MAP);
    EXPECT_TRUE(boolSetting1.value());
    EXPECT_EQ(settings::source::MAP, boolSetting1.get_source());

    {
        settings::setting_container settings2;
        settings::setting_bool boolSetting2(&settings2, "boolSetting", false);
        EXPECT_FALSE(boolSetting2.value());

        settings2.copy_from(settings1);
        EXPECT_TRUE(boolSetting2.value());
        EXPECT_EQ(settings::source::MAP, boolSetting2.get_source());
    }
}

const settings::setting_group test_group{"Test", 0, settings::expected_source::commandline};

TEST(settings, copyContainerSubclass)
{
    struct my_settings : public settings::setting_container
    {
        settings::setting_bool boolSetting{this, "boolSetting", false, &test_group};
        settings::setting_string stringSetting{this, "stringSetting", "default", "\"str\"", &test_group};
    };

    static_assert(!std::is_copy_constructible_v<settings::setting_container>);
    static_assert(!std::is_copy_constructible_v<settings::setting_bool>);
    static_assert(!std::is_copy_constructible_v<my_settings>);

    my_settings s1;
    EXPECT_EQ(&s1.boolSetting, s1.find_setting("boolSetting"));
    EXPECT_EQ(&s1.stringSetting, s1.find_setting("stringSetting"));
    EXPECT_EQ(1, s1.grouped().size());
    EXPECT_EQ((std::set<settings::setting_base *>{&s1.boolSetting, &s1.stringSetting}), s1.grouped().at(&test_group));
    s1.boolSetting.set_value(true, settings::source::MAP);
    EXPECT_EQ(settings::source::MAP, s1.boolSetting.get_source());

    my_settings s2;
    s2.copy_from(s1);
    EXPECT_EQ(&s2.boolSetting, s2.find_setting("boolSetting"));
    EXPECT_EQ(s2.grouped().size(), 1);
    EXPECT_EQ((std::set<settings::setting_base *>{&s2.boolSetting, &s2.stringSetting}), s2.grouped().at(&test_group));
    EXPECT_TRUE(s2.boolSetting.value());
    EXPECT_EQ(settings::source::MAP, s2.boolSetting.get_source());

    // s2.stringSetting is still at its default
    EXPECT_EQ("default", s2.stringSetting.value());
    EXPECT_EQ(settings::source::DEFAULT, s2.stringSetting.get_source());
}

TEST(settings, resetBool)
{
    settings::setting_container settings;
    settings::setting_bool boolSetting1(&settings, "boolSetting", false);

    boolSetting1.set_value(true, settings::source::MAP);
    EXPECT_EQ(settings::source::MAP, boolSetting1.get_source());
    EXPECT_TRUE(boolSetting1.value());

    boolSetting1.reset();
    EXPECT_EQ(settings::source::DEFAULT, boolSetting1.get_source());
    EXPECT_FALSE(boolSetting1.value());
}

TEST(settings, resetScalar)
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting1(&settings, "scalarSetting", 12.34);

    scalarSetting1.set_value(-2, settings::source::MAP);
    EXPECT_EQ(settings::source::MAP, scalarSetting1.get_source());
    EXPECT_EQ(-2.0f, scalarSetting1.value());

    scalarSetting1.reset();
    EXPECT_EQ(settings::source::DEFAULT, scalarSetting1.get_source());
    EXPECT_EQ(12.34f, scalarSetting1.value());
}

TEST(settings, resetContainer)
{
    settings::setting_container settings;
    settings::setting_vec3 vec3Setting1(&settings, "vec", 3, 4, 5);
    settings::setting_string stringSetting1(&settings, "name", "abc");

    vec3Setting1.set_value(qvec3d(-1, -2, -3), settings::source::MAP);
    stringSetting1.set_value("test", settings::source::MAP);
    settings.reset();

    EXPECT_EQ(settings::source::DEFAULT, vec3Setting1.get_source());
    EXPECT_EQ(qvec3f(3, 4, 5), vec3Setting1.value());

    EXPECT_EQ(settings::source::DEFAULT, stringSetting1.get_source());
    EXPECT_EQ("abc", stringSetting1.value());
}

#include "common/polylib.hh"

struct winding_check_t : polylib::winding_base_t<polylib::winding_storage_hybrid_t<double, 4>>
{
public:
    inline size_t vector_size() { return storage.vector_size(); }
};

TEST(polylib, windingIterators)
{
    winding_check_t winding;

    EXPECT_EQ(winding.begin(), winding.end());

    winding.emplace_back(0, 0, 0);

    EXPECT_NE(winding.begin(), winding.end());

    winding.emplace_back(1, 1, 1);
    winding.emplace_back(2, 2, 2);
    winding.emplace_back(3, 3, 3);

    EXPECT_EQ(winding.size(), 4);

    EXPECT_EQ(winding.vector_size(), 0);

    // check that iterators match up before expansion
    {
        auto it = winding.begin();

        for (size_t i = 0; i < winding.size(); i++) {
            EXPECT_EQ((*it)[0], i);

            EXPECT_EQ(it, (winding.begin() + i));

            it++;
        }

        EXPECT_EQ(it, winding.end());
    }

    winding.emplace_back(4, 4, 4);
    winding.emplace_back(5, 5, 5);

    // check that iterators match up after expansion
    {
        auto it = winding.begin();

        for (size_t i = 0; i < winding.size(); i++) {
            EXPECT_EQ((*it)[0], i);

            auto composed_it = winding.begin() + i;
            EXPECT_EQ(it, composed_it);

            it++;
        }

        EXPECT_EQ(it, winding.end());
    }

    // check that constructors work
    {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<double, 4>> winding_other(
            winding.begin(), winding.end());

        {
            auto it = winding_other.begin();

            for (size_t i = 0; i < winding_other.size(); i++) {
                EXPECT_EQ((*it)[0], i);

                auto composed_it = winding_other.begin() + i;
                EXPECT_EQ(it, composed_it);

                it++;
            }

            EXPECT_EQ(it, winding_other.end());
        }
    }

    {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<double, 4>> winding_other(
            {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}, {4, 4, 4}});

        {
            auto it = winding_other.begin();

            for (size_t i = 0; i < winding_other.size(); i++) {
                EXPECT_EQ((*it)[0], i);

                auto composed_it = winding_other.begin() + i;
                EXPECT_EQ(it, composed_it);

                it++;
            }

            EXPECT_EQ(it, winding_other.end());
        }
    }

    {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<double, 4>> winding_other(std::move(winding));

        EXPECT_EQ(winding.size(), 0);
        EXPECT_EQ(winding.begin(), winding.end());

        {
            auto it = winding_other.begin();

            for (size_t i = 0; i < winding_other.size(); i++) {
                EXPECT_EQ((*it)[0], i);

                auto composed_it = winding_other.begin() + i;
                EXPECT_EQ(it, composed_it);

                it++;
            }

            EXPECT_EQ(it, winding_other.end());
        }
    }
}
