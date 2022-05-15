#include <Catch2/catch.hpp>

#include <filesystem>
#include <string>
#include <common/cmdlib.hh>

TEST(common, StripFilename)
{
    ASSERT_TRUE("/home/foo" == fs::path("/home/foo/bar.txt").parent_path());
    ASSERT_TRUE("" == fs::path("bar.txt").parent_path());
}
