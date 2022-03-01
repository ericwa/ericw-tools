#include "gtest/gtest.h"

#include <filesystem>
#include <string>
#include <common/cmdlib.hh>

TEST(common, StripFilename)
{
    ASSERT_EQ("/home/foo", fs::path("/home/foo/bar.txt").parent_path());
    ASSERT_EQ("", fs::path("bar.txt").parent_path());
}
