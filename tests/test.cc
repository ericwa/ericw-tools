#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "common/settings.hh"

#include <type_traits>

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

class TestRunListener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(Catch::TestRunInfo const&) override {
        // writing console colors within test case output breaks Catch2/CLion integration
        logging::enable_color_codes = false;
    }
};

CATCH_REGISTER_LISTENER(TestRunListener)

// test booleans
TEST_CASE("booleanFlagImplicit", "[settings]")
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(boolSetting.value() == true);
}

TEST_CASE("booleanFlagExplicit", "[settings]")
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "1"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(boolSetting.value() == true);
}

TEST_CASE("booleanFlagStray", "[settings]")
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    const char *arguments[] = {"qbsp.exe", "-locked", "stray"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(boolSetting.value() == true);
}

// test scalars
TEST_CASE("scalarSimple", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(scalarSetting.value() == 1.25);
}

TEST_CASE("scalarNegative", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "-0.25"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(scalarSetting.value() == -0.25);
}

TEST_CASE("scalarInfinity", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0, 0.0, std::numeric_limits<vec_t>::infinity());
    const char *arguments[] = {"qbsp.exe", "-scale", "INFINITY"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(scalarSetting.value() == std::numeric_limits<vec_t>::infinity());
}

TEST_CASE("scalarNAN", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "NAN"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(std::isnan(scalarSetting.value()));
}

TEST_CASE("scalarScientific", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "1.54334E-34"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(scalarSetting.value() == 1.54334E-34);
}

TEST_CASE("scalarEOF", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
}

TEST_CASE("scalarStray", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
    const char *arguments[] = {"qbsp.exe", "-scale", "stray"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
}

// test scalars
TEST_CASE("vec3Simple", "[settings]")
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "3"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(scalarSetting.value() == (qvec3d{1, 2, 3}));
}

TEST_CASE("vec3Complex", "[settings]")
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "-12.5", "-INFINITY", "NAN"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(scalarSetting.value()[0] == -12.5);
    REQUIRE(scalarSetting.value()[1] == -std::numeric_limits<vec_t>::infinity());
    REQUIRE(std::isnan(scalarSetting.value()[2]));
}

TEST_CASE("vec3Incomplete", "[settings]")
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
}

TEST_CASE("vec3Stray", "[settings]")
{
    settings::setting_container settings;
    settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
    const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "abc"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
}

// test string formatting
TEST_CASE("stringSimple", "[settings]")
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "-name", "i am a string with spaces in it"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(stringSetting.value() == arguments[2]);
}

// test remainder
TEST_CASE("remainder", "[settings]")
{
    settings::setting_container settings;
    settings::setting_string stringSetting(&settings, "name", "");
    settings::setting_bool flagSetting(&settings, "flag", false);
    const char *arguments[] = {
        "qbsp.exe", "-name", "string", "-flag", "remainder one", "remainder two"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    auto remainder = settings.parse(p);
    REQUIRE(remainder[0] == "remainder one");
    REQUIRE(remainder[1] == "remainder two");
}

// test double-hyphens
TEST_CASE("doubleHyphen", "[settings]")
{
    settings::setting_container settings;
    settings::setting_bool boolSetting(&settings, "locked", false);
    settings::setting_string stringSetting(&settings, "name", "");
    const char *arguments[] = {"qbsp.exe", "--locked", "--name", "my name!"};
    token_parser_t p{std::size(arguments) - 1, arguments + 1, { }};
    settings.parse(p);
    REQUIRE(boolSetting.value() == true);
    REQUIRE(stringSetting.value() == "my name!");
}

// test groups; ensure that performance is the first group
TEST_CASE("grouping", "[settings]")
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
    REQUIRE(settings.grouped().begin()->first == &performance);
    // settings.printHelp();
}

TEST_CASE("copy", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scaleSetting(&settings, "scale", 1.5);
    settings::setting_scalar waitSetting(&settings, "wait", 0.0);
    settings::setting_string stringSetting(&settings, "string", "test");

    CHECK(settings::source::DEFAULT == scaleSetting.getSource());
    CHECK(settings::source::DEFAULT == waitSetting.getSource());
    CHECK(0 == waitSetting.value());

    CHECK(waitSetting.copyFrom(scaleSetting));
    CHECK(settings::source::DEFAULT == waitSetting.getSource());
    CHECK(1.5 == waitSetting.value());

    // if copy fails, the value remains unchanged
    CHECK_FALSE(waitSetting.copyFrom(stringSetting));
    CHECK(settings::source::DEFAULT == waitSetting.getSource());
    CHECK(1.5 == waitSetting.value());

    scaleSetting.setValue(2.5, settings::source::MAP);
    CHECK(settings::source::MAP == scaleSetting.getSource());

    // source is also copied
    CHECK(waitSetting.copyFrom(scaleSetting));
    CHECK(settings::source::MAP == waitSetting.getSource());
    CHECK(2.5 == waitSetting.value());
}

TEST_CASE("copyMangle", "[settings]")
{
    settings::setting_container settings;
    settings::setting_mangle sunvec{&settings, {"sunlight_mangle"}, 0.0, 0.0, 0.0};

    parser_t p(std::string_view("0.0 -90.0 0.0"), { });
    CHECK(sunvec.parse("", p, settings::source::COMMANDLINE));
    CHECK(Catch::Approx(0).margin(1e-6) == sunvec.value()[0]);
    CHECK(Catch::Approx(0).margin(1e-6) == sunvec.value()[1]);
    CHECK(Catch::Approx(-1).margin(1e-6) == sunvec.value()[2]);

    settings::setting_mangle sunvec2{&settings, {"sunlight_mangle2"}, 0.0, 0.0, 0.0};
    sunvec2.copyFrom(sunvec);

    CHECK(Catch::Approx(0).margin(1e-6) == sunvec2.value()[0]);
    CHECK(Catch::Approx(0).margin(1e-6) == sunvec2.value()[1]);
    CHECK(Catch::Approx(-1).margin(1e-6) == sunvec2.value()[2]);
}

TEST_CASE("copyContainer", "[settings]")
{
    settings::setting_container settings1;
    settings::setting_bool boolSetting1(&settings1, "boolSetting", false);
    CHECK_FALSE(boolSetting1.value());
    CHECK(settings::source::DEFAULT == boolSetting1.getSource());

    boolSetting1.setValue(true, settings::source::MAP);
    CHECK(boolSetting1.value());
    CHECK(settings::source::MAP == boolSetting1.getSource());

    {
        settings::setting_container settings2;
        settings::setting_bool boolSetting2(&settings2, "boolSetting", false);
        CHECK_FALSE(boolSetting2.value());

        settings2.copyFrom(settings1);
        CHECK(boolSetting2.value());
        CHECK(settings::source::MAP == boolSetting2.getSource());
    }
}

const settings::setting_group test_group{"Test"};

TEST_CASE("copyContainerSubclass", "[settings]")
{
    struct my_settings : public settings::setting_container {
        settings::setting_bool boolSetting {this, "boolSetting", false, &test_group};
        settings::setting_string stringSetting {this, "stringSetting", "default", "\"str\"", &test_group};
    };

    static_assert(!std::is_copy_constructible_v<settings::setting_container>);
    static_assert(!std::is_copy_constructible_v<settings::setting_bool>);
    static_assert(!std::is_copy_constructible_v<my_settings>);

    my_settings s1;
    CHECK(&s1.boolSetting == s1.findSetting("boolSetting"));
    CHECK(&s1.stringSetting == s1.findSetting("stringSetting"));
    CHECK(1 == s1.grouped().size());
    CHECK((std::set<settings::setting_base *>{ &s1.boolSetting, &s1.stringSetting }) == s1.grouped().at(&test_group));
    s1.boolSetting.setValue(true, settings::source::MAP);
    CHECK(settings::source::MAP == s1.boolSetting.getSource());

    my_settings s2;
    s2.copyFrom(s1);
    CHECK(&s2.boolSetting == s2.findSetting("boolSetting"));
    CHECK(s2.grouped().size() == 1);
    CHECK((std::set<settings::setting_base *>{ &s2.boolSetting, &s2.stringSetting }) == s2.grouped().at(&test_group));
    CHECK(s2.boolSetting.value());
    CHECK(settings::source::MAP == s2.boolSetting.getSource());

    // s2.stringSetting is still at its default
    CHECK("default" == s2.stringSetting.value());
    CHECK(settings::source::DEFAULT == s2.stringSetting.getSource());
}

TEST_CASE("resetBool", "[settings]")
{
    settings::setting_container settings;
    settings::setting_bool boolSetting1(&settings, "boolSetting", false);

    boolSetting1.setValue(true, settings::source::MAP);
    CHECK(settings::source::MAP == boolSetting1.getSource());
    CHECK(boolSetting1.value());

    boolSetting1.reset();
    CHECK(settings::source::DEFAULT == boolSetting1.getSource());
    CHECK_FALSE(boolSetting1.value());
}

TEST_CASE("resetScalar", "[settings]")
{
    settings::setting_container settings;
    settings::setting_scalar scalarSetting1(&settings, "scalarSetting", 12.34);

    scalarSetting1.setValue(-2, settings::source::MAP);
    CHECK(settings::source::MAP == scalarSetting1.getSource());
    CHECK(-2 == scalarSetting1.value());

    scalarSetting1.reset();
    CHECK(settings::source::DEFAULT == scalarSetting1.getSource());
    CHECK(12.34 == scalarSetting1.value());
}

TEST_CASE("resetContainer", "[settings]")
{
    settings::setting_container settings;
    settings::setting_vec3 vec3Setting1(&settings, "vec", 3, 4, 5);
    settings::setting_string stringSetting1(&settings, "name", "abc");

    vec3Setting1.setValue(qvec3d(-1, -2, -3), settings::source::MAP);
    stringSetting1.setValue("test", settings::source::MAP);
    settings.reset();

    CHECK(settings::source::DEFAULT == vec3Setting1.getSource());
    CHECK(qvec3d(3, 4, 5) == vec3Setting1.value());

    CHECK(settings::source::DEFAULT == stringSetting1.getSource());
    CHECK("abc" == stringSetting1.value());
}

#include "common/polylib.hh"

struct winding_check_t : polylib::winding_base_t<polylib::winding_storage_hybrid_t<4>>
{
public:
    inline size_t vector_size() { return storage.vector_size(); }
};

TEST_CASE("winding iterators", "[winding_base_t]")
{
    winding_check_t winding;

    CHECK(winding.begin() == winding.end());
    
    winding.emplace_back(0, 0, 0);

    CHECK(winding.begin() != winding.end());
    
    winding.emplace_back(1, 1, 1);
    winding.emplace_back(2, 2, 2);
    winding.emplace_back(3, 3, 3);

    CHECK(winding.size() == 4);

    CHECK(winding.vector_size() == 0);

    // check that iterators match up before expansion
    {
        auto it = winding.begin();

        for (size_t i = 0; i < winding.size(); i++) {
            CHECK((*it)[0] == i);

            CHECK(it == (winding.begin() + i));

            it++;
        }

        CHECK(it == winding.end());
    }
    
    winding.emplace_back(4, 4, 4);
    winding.emplace_back(5, 5, 5);

    // check that iterators match up after expansion
    {
        auto it = winding.begin();

        for (size_t i = 0; i < winding.size(); i++) {
            CHECK((*it)[0] == i);

            auto composed_it = winding.begin() + i;
            CHECK(it == composed_it);

            it++;
        }

        CHECK(it == winding.end());
    }

    // check that constructors work
    {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<4>> winding_other(winding.begin(), winding.end());

        {
            auto it = winding_other.begin();

            for (size_t i = 0; i < winding_other.size(); i++) {
                CHECK((*it)[0] == i);

                auto composed_it = winding_other.begin() + i;
                CHECK(it == composed_it);

                it++;
            }

            CHECK(it == winding_other.end());
        }
    }


    {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<4>> winding_other({ { 0, 0, 0 }, { 1, 1, 1 }, { 2, 2, 2 }, { 3, 3, 3 }, { 4, 4, 4 } });

        {
            auto it = winding_other.begin();

            for (size_t i = 0; i < winding_other.size(); i++) {
                CHECK((*it)[0] == i);

                auto composed_it = winding_other.begin() + i;
                CHECK(it == composed_it);

                it++;
            }

            CHECK(it == winding_other.end());
        }
    }

    {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<4>> winding_other(std::move(winding));

        CHECK(winding.size() == 0);
        CHECK(winding.begin() == winding.end());

        {
            auto it = winding_other.begin();

            for (size_t i = 0; i < winding_other.size(); i++) {
                CHECK((*it)[0] == i);

                auto composed_it = winding_other.begin() + i;
                CHECK(it == composed_it);

                it++;
            }

            CHECK(it == winding_other.end());
        }
    }
}
