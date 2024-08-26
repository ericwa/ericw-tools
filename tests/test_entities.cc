#include <gtest/gtest.h>

#include <light/entities.hh>

TEST(entities, checkEmptyValues)
{
    entdict_t good1{};
    entdict_t good2{{"foo", "bar"}};
    entdict_t bad1{{"foo", ""}};
    entdict_t bad2{{"", "bar"}};
    entdict_t bad3{{"", ""}};

    EXPECT_TRUE(EntDict_CheckNoEmptyValues(nullptr, good1));
    EXPECT_TRUE(EntDict_CheckNoEmptyValues(nullptr, good2));
    EXPECT_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad1));
    EXPECT_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad2));
    EXPECT_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad3));
}
