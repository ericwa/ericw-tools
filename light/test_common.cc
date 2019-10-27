#include "gtest/gtest.h"

#include <string>
#include <common/cmdlib.hh>

TEST(common, StripFilename) {
    char test[] = "/home/foo/bar.txt";
    StripFilename(test);
    
    ASSERT_EQ(std::string("/home/foo"), std::string(test));
}
