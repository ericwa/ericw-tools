#include <nanobench.h>
#include <doctest/doctest.h>
#include <vis/vis.hh>
#include <common/qvec.hh>
#include <common/polylib.hh>

#include <array>
#include <vector>

TEST_CASE("winding" * doctest::test_suite("benchmark") * doctest::skip())
{
    ankerl::nanobench::Bench bench;

    bench.run("std::vector<double> reserve(3*4*6)", [&] {
        std::vector<double> temp;
        temp.reserve(3 * 4 * 6);
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("std::vector<qvec3d> reserve(4*6)", [&] {
        std::vector<qvec3d> temp;
        temp.reserve(4 * 6);
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("std::array<double, 3*4*6>", [&] {
        std::array<double, 3 * 4 * 6> temp;
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("std::array<qvec3d, 4*6>", [&] {
        std::array<qvec3d, 4 * 6> temp;
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
    bench.run("polylib::winding_base_t<6> construct", [&] {
        polylib::winding_base_t<polylib::winding_storage_hybrid_t<6>> temp;
        ankerl::nanobench::doNotOptimizeAway(temp);
    });
}

static void test_polylib(bool check_results)
{
    polylib::winding_t w(4);

    // top face to TB default brush
    w[0] = {-64, 64, 16};
    w[1] = {64, 64, 16};
    w[2] = {64, -64, 16};
    w[3] = {-64, -64, 16};

    qplane3d splitplane{{1, 0, 0}, 0};
    auto [front, back] = w.clip(splitplane);

    ankerl::nanobench::doNotOptimizeAway(front);
    ankerl::nanobench::doNotOptimizeAway(back);

    if (check_results) {
        REQUIRE(front);
        REQUIRE(back);

        CHECK(front->size() == 4);
        CHECK(back->size() == 4);

        // check front polygon
        CHECK(front->at(0) == qvec3d{0, 64, 16});
        CHECK(front->at(1) == qvec3d{64, 64, 16});
        CHECK(front->at(2) == qvec3d{64, -64, 16});
        CHECK(front->at(3) == qvec3d{0, -64, 16});

        // check back polygon
        CHECK(back->at(0) == qvec3d{-64, 64, 16});
        CHECK(back->at(1) == qvec3d{0, 64, 16});
        CHECK(back->at(2) == qvec3d{0, -64, 16});
        CHECK(back->at(3) == qvec3d{-64, -64, 16});
    }
}

TEST_CASE("SplitFace" * doctest::test_suite("benchmark"))
{
    ankerl::nanobench::Bench().run("create and split a face (polylib)", [&]() { test_polylib(false); });

    // run with doctest assertions, to validate that they actually work
    test_polylib(true);
}

TEST_CASE("vis windings")
{
    ankerl::nanobench::Bench b;
    b.run("create pstack_t", [&]() {
        pstack_t stack;
        for (int i=0; i<3; ++i)
            stack.windings_used[i] = false;

        ankerl::nanobench::doNotOptimizeAway(stack);
    });

    b.run("create pstack_t + 1x AllocStackWinding", [&]() {
        pstack_t stack;
        for (int i=0; i<3; ++i)
            stack.windings_used[i] = false;

        auto *w1 = AllocStackWinding(stack);
        ankerl::nanobench::doNotOptimizeAway(*w1);

        FreeStackWinding(w1, stack);

        ankerl::nanobench::doNotOptimizeAway(stack);
    });

    b.run("create pstack_t + 2x AllocStackWinding", [&]() {
        pstack_t stack;
        for (int i=0; i<3; ++i)
            stack.windings_used[i] = false;

        auto *w1 = AllocStackWinding(stack);
        ankerl::nanobench::doNotOptimizeAway(*w1);

        auto *w2 = AllocStackWinding(stack);
        ankerl::nanobench::doNotOptimizeAway(*w2);

        FreeStackWinding(w1, stack);
        FreeStackWinding(w2, stack);

        ankerl::nanobench::doNotOptimizeAway(stack);
    });

    b.run("setup + ClipStackWinding", [&]() {
        visstats_t stats;
        pstack_t stack;
        for (int i=0; i<3; ++i)
            stack.windings_used[i] = false;

        auto *w1 = AllocStackWinding(stack);
        w1->numpoints = 4;
        w1->points[0] = {0, 0, 0};
        w1->points[1] = {32, 0, 0};
        w1->points[2] = {32, 0, -32};
        w1->points[3] = {0, 0, -32};
        w1->set_winding_sphere();

        w1 = ClipStackWinding(stats, w1, stack, qplane3d({-1, 0, 0}, -16));
        ankerl::nanobench::doNotOptimizeAway(*w1);

        FreeStackWinding(w1, stack);
        ankerl::nanobench::doNotOptimizeAway(stack);
    });
}

TEST_CASE("vector math")
{
    ankerl::nanobench::Bench b;
    ankerl::nanobench::Rng rng;

    qvec3d vec0 {rng.uniform01(), rng.uniform01(), rng.uniform01()};
    qvec3d vec1 {rng.uniform01(), rng.uniform01(), rng.uniform01()};

    b.run("dot product", [&]() {
        vec0[0] = qv::dot(vec0, vec1);
    });
    b.run("add", [&]() {
        vec0 = vec0 + vec1;
    });
    b.run("subtract", [&]() {
        vec0 = vec0 - vec1;
    });

    b.doNotOptimizeAway(vec0);
    b.doNotOptimizeAway(vec1);
}
