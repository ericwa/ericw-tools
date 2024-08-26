#pragma once

#include <gtest/gtest.h>
#include <algorithm>

template<class A>
void EXPECT_VECTORS_UNOREDERED_EQUAL(const A &a, const A &b)
{
    if (a.size() != b.size()) {
        ADD_FAILURE() << "Expected vectors to be equal (ignoring order)";
        return;
    }

    for (auto &a_elem : a) {
        if (std::find(b.begin(), b.end(), a_elem) == b.end()) {
            ADD_FAILURE() << "Expected vectors to be equal (ignoring order)";
            return;
        }
    }
}
