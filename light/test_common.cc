#include "gtest/gtest.h"

#include <filesystem>
#include <string>
#include <common/cmdlib.hh>

TEST(common, StripFilename)
{
    ASSERT_EQ("/home/foo", std::filesystem::path("/home/foo/bar.txt").parent_path());
    ASSERT_EQ("", std::filesystem::path("bar.txt").parent_path());
}
