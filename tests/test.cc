#include <doctest/doctest.h>

#include "common/settings.hh"

#include <type_traits>

TEST_SUITE("settings")
{

    // test booleans
    TEST_CASE("booleanFlagImplicit")
    {
        settings::setting_container settings;
        settings::setting_bool boolSetting(&settings, "locked", false);
        const char *arguments[] = {"qbsp.exe", "-locked"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(boolSetting.value() == true);
    }

    TEST_CASE("booleanFlagExplicit")
    {
        settings::setting_container settings;
        settings::setting_bool boolSetting(&settings, "locked", false);
        const char *arguments[] = {"qbsp.exe", "-locked", "1"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(boolSetting.value() == true);
    }

    TEST_CASE("booleanFlagStray")
    {
        settings::setting_container settings;
        settings::setting_bool boolSetting(&settings, "locked", false);
        const char *arguments[] = {"qbsp.exe", "-locked", "stray"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(boolSetting.value() == true);
    }

    // test scalars
    TEST_CASE("scalarSimple")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
        const char *arguments[] = {"qbsp.exe", "-scale", "1.25"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(scalarSetting.value() == 1.25f);
    }

    TEST_CASE("scalarNegative")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
        const char *arguments[] = {"qbsp.exe", "-scale", "-0.25"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(scalarSetting.value() == -0.25f);
    }

    TEST_CASE("scalarInfinity")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0, 0.0, std::numeric_limits<vec_t>::infinity());
        const char *arguments[] = {"qbsp.exe", "-scale", "INFINITY"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(scalarSetting.value() == std::numeric_limits<float>::infinity());
    }

    TEST_CASE("scalarNAN")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
        const char *arguments[] = {"qbsp.exe", "-scale", "NAN"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(std::isnan(scalarSetting.value()));
    }

    TEST_CASE("scalarScientific")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
        const char *arguments[] = {"qbsp.exe", "-scale", "1.54334E-34"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(scalarSetting.value() == 1.54334E-34f);
    }

    TEST_CASE("scalarEOF")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
        const char *arguments[] = {"qbsp.exe", "-scale"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
    }

    TEST_CASE("scalarStray")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting(&settings, "scale", 1.0);
        const char *arguments[] = {"qbsp.exe", "-scale", "stray"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
    }

    // test scalars
    TEST_CASE("vec3Simple")
    {
        settings::setting_container settings;
        settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
        const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "3"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(scalarSetting.value() == (qvec3f{1, 2, 3}));
    }

    TEST_CASE("vec3Complex")
    {
        settings::setting_container settings;
        settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
        const char *arguments[] = {"qbsp.exe", "-origin", "-12.5", "-INFINITY", "NAN"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(scalarSetting.value()[0] == -12.5f);
        REQUIRE(scalarSetting.value()[1] == -std::numeric_limits<float>::infinity());
        REQUIRE(std::isnan(scalarSetting.value()[2]));
    }

    TEST_CASE("vec3Incomplete")
    {
        settings::setting_container settings;
        settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
        const char *arguments[] = {"qbsp.exe", "-origin", "1", "2"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
    }

    TEST_CASE("vec3Stray")
    {
        settings::setting_container settings;
        settings::setting_vec3 scalarSetting(&settings, "origin", 0, 0, 0);
        const char *arguments[] = {"qbsp.exe", "-origin", "1", "2", "abc"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        REQUIRE_THROWS_AS(settings.parse(p), settings::parse_exception);
    }

    // test string formatting
    TEST_CASE("stringSimple")
    {
        settings::setting_container settings;
        settings::setting_string stringSetting(&settings, "name", "");
        const char *arguments[] = {"qbsp.exe", "-name", "i am a string with spaces in it"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(stringSetting.value() == arguments[2]);
    }

    // test remainder
    TEST_CASE("remainder")
    {
        settings::setting_container settings;
        settings::setting_string stringSetting(&settings, "name", "");
        settings::setting_bool flagSetting(&settings, "flag", false);
        const char *arguments[] = {"qbsp.exe", "-name", "string", "-flag", "remainder one", "remainder two"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        auto remainder = settings.parse(p);
        REQUIRE(remainder[0] == "remainder one");
        REQUIRE(remainder[1] == "remainder two");
    }

    // test double-hyphens
    TEST_CASE("doubleHyphen")
    {
        settings::setting_container settings;
        settings::setting_bool boolSetting(&settings, "locked", false);
        settings::setting_string stringSetting(&settings, "name", "");
        const char *arguments[] = {"qbsp.exe", "--locked", "--name", "my name!"};
        token_parser_t p{std::size(arguments) - 1, arguments + 1, {}};
        settings.parse(p);
        REQUIRE(boolSetting.value() == true);
        REQUIRE(stringSetting.value() == "my name!");
    }

    // test groups; ensure that performance is the first group
    TEST_CASE("grouping")
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

    TEST_CASE("copy")
    {
        settings::setting_container settings;
        settings::setting_scalar scaleSetting(&settings, "scale", 1.5);
        settings::setting_scalar waitSetting(&settings, "wait", 0.0);
        settings::setting_string stringSetting(&settings, "string", "test");

        CHECK(settings::source::DEFAULT == scaleSetting.get_source());
        CHECK(settings::source::DEFAULT == waitSetting.get_source());
        CHECK(0 == waitSetting.value());

        CHECK(waitSetting.copy_from(scaleSetting));
        CHECK(settings::source::DEFAULT == waitSetting.get_source());
        CHECK(1.5 == waitSetting.value());

        // if copy fails, the value remains unchanged
        CHECK_FALSE(waitSetting.copy_from(stringSetting));
        CHECK(settings::source::DEFAULT == waitSetting.get_source());
        CHECK(1.5 == waitSetting.value());

        scaleSetting.set_value(2.5, settings::source::MAP);
        CHECK(settings::source::MAP == scaleSetting.get_source());

        // source is also copied
        CHECK(waitSetting.copy_from(scaleSetting));
        CHECK(settings::source::MAP == waitSetting.get_source());
        CHECK(2.5 == waitSetting.value());
    }

    TEST_CASE("copyMangle")
    {
        settings::setting_container settings;
        settings::setting_mangle sunvec{&settings, {"sunlight_mangle"}, 0.0, 0.0, 0.0};

        parser_t p(std::string_view("0.0 -90.0 0.0"), {});
        CHECK(sunvec.parse("", p, settings::source::COMMANDLINE));
        CHECK(doctest::Approx(0) == sunvec.value()[0]);
        CHECK(doctest::Approx(0) == sunvec.value()[1]);
        CHECK(doctest::Approx(-1) == sunvec.value()[2]);

        settings::setting_mangle sunvec2{&settings, {"sunlight_mangle2"}, 0.0, 0.0, 0.0};
        sunvec2.copy_from(sunvec);

        CHECK(doctest::Approx(0) == sunvec2.value()[0]);
        CHECK(doctest::Approx(0) == sunvec2.value()[1]);
        CHECK(doctest::Approx(-1) == sunvec2.value()[2]);
    }

    TEST_CASE("copyContainer")
    {
        settings::setting_container settings1;
        settings::setting_bool boolSetting1(&settings1, "boolSetting", false);
        CHECK_FALSE(boolSetting1.value());
        CHECK(settings::source::DEFAULT == boolSetting1.get_source());

        boolSetting1.set_value(true, settings::source::MAP);
        CHECK(boolSetting1.value());
        CHECK(settings::source::MAP == boolSetting1.get_source());

        {
            settings::setting_container settings2;
            settings::setting_bool boolSetting2(&settings2, "boolSetting", false);
            CHECK_FALSE(boolSetting2.value());

            settings2.copy_from(settings1);
            CHECK(boolSetting2.value());
            CHECK(settings::source::MAP == boolSetting2.get_source());
        }
    }

    const settings::setting_group test_group{"Test", 0, settings::expected_source::commandline};

    TEST_CASE("copyContainerSubclass")
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
        CHECK(&s1.boolSetting == s1.find_setting("boolSetting"));
        CHECK(&s1.stringSetting == s1.find_setting("stringSetting"));
        CHECK(1 == s1.grouped().size());
        CHECK((std::set<settings::setting_base *>{&s1.boolSetting, &s1.stringSetting}) == s1.grouped().at(&test_group));
        s1.boolSetting.set_value(true, settings::source::MAP);
        CHECK(settings::source::MAP == s1.boolSetting.get_source());

        my_settings s2;
        s2.copy_from(s1);
        CHECK(&s2.boolSetting == s2.find_setting("boolSetting"));
        CHECK(s2.grouped().size() == 1);
        CHECK((std::set<settings::setting_base *>{&s2.boolSetting, &s2.stringSetting}) == s2.grouped().at(&test_group));
        CHECK(s2.boolSetting.value());
        CHECK(settings::source::MAP == s2.boolSetting.get_source());

        // s2.stringSetting is still at its default
        CHECK("default" == s2.stringSetting.value());
        CHECK(settings::source::DEFAULT == s2.stringSetting.get_source());
    }

    TEST_CASE("resetBool")
    {
        settings::setting_container settings;
        settings::setting_bool boolSetting1(&settings, "boolSetting", false);

        boolSetting1.set_value(true, settings::source::MAP);
        CHECK(settings::source::MAP == boolSetting1.get_source());
        CHECK(boolSetting1.value());

        boolSetting1.reset();
        CHECK(settings::source::DEFAULT == boolSetting1.get_source());
        CHECK_FALSE(boolSetting1.value());
    }

    TEST_CASE("resetScalar")
    {
        settings::setting_container settings;
        settings::setting_scalar scalarSetting1(&settings, "scalarSetting", 12.34);

        scalarSetting1.set_value(-2, settings::source::MAP);
        CHECK(settings::source::MAP == scalarSetting1.get_source());
        CHECK(-2.0f == scalarSetting1.value());

        scalarSetting1.reset();
        CHECK(settings::source::DEFAULT == scalarSetting1.get_source());
        CHECK(12.34f == scalarSetting1.value());
    }

    TEST_CASE("resetContainer")
    {
        settings::setting_container settings;
        settings::setting_vec3 vec3Setting1(&settings, "vec", 3, 4, 5);
        settings::setting_string stringSetting1(&settings, "name", "abc");

        vec3Setting1.set_value(qvec3d(-1, -2, -3), settings::source::MAP);
        stringSetting1.set_value("test", settings::source::MAP);
        settings.reset();

        CHECK(settings::source::DEFAULT == vec3Setting1.get_source());
        CHECK(qvec3f(3, 4, 5) == vec3Setting1.value());

        CHECK(settings::source::DEFAULT == stringSetting1.get_source());
        CHECK("abc" == stringSetting1.value());
    }

} // settings

#include "common/polylib.hh"

struct winding_check_t : polylib::winding_base_t<polylib::winding_storage_hybrid_t<4>>
{
public:
    inline size_t vector_size() { return storage.vector_size(); }
};

TEST_SUITE("winding_base_t")
{

    TEST_CASE("winding iterators")
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
            polylib::winding_base_t<polylib::winding_storage_hybrid_t<4>> winding_other(
                {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}, {4, 4, 4}});

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
}
