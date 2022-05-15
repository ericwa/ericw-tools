#include <catch2/catch.hpp>

#include <light/entities.hh>
#include <vector>

TEST_CASE("CheckEmptyValues", "[entities]")
{
    entdict_t good1{};
    entdict_t good2{{"foo", "bar"}};
    entdict_t bad1{{"foo", ""}};
    entdict_t bad2{{"", "bar"}};
    entdict_t bad3{{"", ""}};

    CHECK(EntDict_CheckNoEmptyValues(nullptr, good1));
    CHECK(EntDict_CheckNoEmptyValues(nullptr, good2));
    CHECK_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad1));
    CHECK_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad2));
    CHECK_FALSE(EntDict_CheckNoEmptyValues(nullptr, bad3));
}

TEST_CASE("CheckTargetKeysMatched", "[entities]")
{
    std::vector<entdict_t> edicts{// good
        {{"target", "matched"}}, {{"target2", "matched"}}, {{"targetname", "matched"}},
        // bad
        {{"target", "unmatched"}}, {{"target", "targets_self"}, {"targetname", "targets_self"}}};
    CHECK(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(0), edicts));
    CHECK(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(1), edicts));
    CHECK(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(2), edicts));
    CHECK_FALSE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(3), edicts));
    CHECK_FALSE(EntDict_CheckTargetKeysMatched(nullptr, edicts.at(4), edicts));
}

TEST_CASE("CheckTargetnameKeyMatched", "[entities]")
{
    std::vector<entdict_t> edicts{// good
        {{"some_mod_specific_target_key", "matched"}}, {{"targetname", "matched"}},
        // bad
        {{"targetname", "unmatched"}}};
    CHECK(EntDict_CheckTargetnameKeyMatched(nullptr, edicts.at(0), edicts));
    CHECK(EntDict_CheckTargetnameKeyMatched(nullptr, edicts.at(1), edicts));
    CHECK_FALSE(EntDict_CheckTargetnameKeyMatched(nullptr, edicts.at(2), edicts));
}
