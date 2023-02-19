#pragma once

#include <doctest/doctest.h>
#include <algorithm>

template<class A>
void CHECK_VECTORS_UNOREDERED_EQUAL(const A &a, const A &b)
{
    if (a.size() != b.size()) {
        FAIL_CHECK("Expected vectors to be equal (ignoring order)");
        return;
    }

    for (auto &a_elem : a) {
        if (std::find(b.begin(), b.end(), a_elem) == b.end()) {
            FAIL_CHECK("Expected vectors to be equal (ignoring order)");
            return;
        }
    }
}
