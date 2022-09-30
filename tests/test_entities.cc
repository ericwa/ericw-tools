#include <catch2/catch_test_macros.hpp>

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
