#include <catch2/catch.hpp>

#include <filesystem>
#include <string>
#include <common/cmdlib.hh>

TEST_CASE("StripFilename", "[common]")
{
    REQUIRE("/home/foo" == fs::path("/home/foo/bar.txt").parent_path());
    REQUIRE("" == fs::path("bar.txt").parent_path());
}
