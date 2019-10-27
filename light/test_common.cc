#include "gtest/gtest.h"

#include <string>
#include <common/cmdlib.hh>

TEST(common, StripFilename) {
    ASSERT_EQ("/home/foo", StrippedFilename("/home/foo/bar.txt"));
    ASSERT_EQ("", StrippedFilename("bar.txt"));
}
