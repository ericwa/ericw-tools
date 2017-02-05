#include "gtest/gtest.h"

#include <light/entities.hh>
#include <vector>

TEST(entities, CheckEmptyValues) {
    entdict_t good1 {};
    entdict_t good2 {{"foo", "bar"}};
    entdict_t bad1 {{"foo", ""}};
    entdict_t bad2 {{"", "bar"}};
    entdict_t bad3 {{"", ""}};
    
    EXPECT_TRUE(EntDict_CheckNoEmptyValues(nullptr, good1));
    EXPECT_TRUE(EntDict_CheckNoEmptyValues(nullptr, good2));
    EXPECT_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad1));
    EXPECT_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad2));
    EXPECT_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad3));
}

TEST(entities, CheckTargetKeysMatched) {
    std::vector<entdict_t> edicts {
        // good
        {
            {"target", "matched" }
        },
        {
            {"target2", "matched" }
        },
        {
            {"targetname", "matched" }
        },
        // bad
        {
            { "target", "unmatched" }
        },
        {
            {"target", "targets_self" },
            {"targetname", "targets_self" }
        }
    };
    EXPECT_TRUE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(0), edicts));
    EXPECT_TRUE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(1), edicts));
    EXPECT_TRUE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(2), edicts));
    EXPECT_FALSE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(3), edicts));
    EXPECT_FALSE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(4), edicts));
}

TEST(entities, CheckTargetnameKeyMatched) {
    std::vector<entdict_t> edicts {
        // good
        {
            {"some_mod_specific_target_key", "matched" }
        },
        {
            {"targetname", "matched" }
        },
        // bad
        {
            { "targetname", "unmatched" }
        }
    };
    EXPECT_TRUE(EntDict_CheckTargetnameKeyMatched(nullptr, edicts.at(0), edicts));
    EXPECT_TRUE(EntDict_CheckTargetnameKeyMatched(nullptr, edicts.at(1), edicts));
    EXPECT_FALSE(EntDict_CheckTargetnameKeyMatched(nullptr, edicts.at(2), edicts));
}
