#include <gtest/gtest.h>

#include <common/log.hh>
#include "light/spatialindex.hh"

static polylib::winding_t make_winding(const qvec3d &origin)
{
    polylib::winding_t w(4);

    //128x128 at (0,0,0), +Z normal
    w[0] = {-64, 64, 0};
    w[1] = {64, 64, 0};
    w[2] = {64, -64, 0};
    w[3] = {-64, -64, 0};

    w = w.translate(origin);

    return w;
}

TEST(lightpreview, basicSpatial)
{
    constexpr float epsilon = 0.001;

    spatialindex_t si;

    si.add_poly(make_winding(qvec3d(0,0,0)), std::string("at 0 0 0"));
    si.add_poly(make_winding(qvec3d(1000,0,0)), std::string("at 1000 0 0"));

    si.commit();

    {
        hitresult_t res = si.trace_ray(qvec3f(1000, 0, 100), qvec3f(0, 0, -1));

        EXPECT_NEAR(res.hitpos[0], 1000, epsilon);
        EXPECT_NEAR(res.hitpos[1], 0, epsilon);
        EXPECT_NEAR(res.hitpos[2], 0, epsilon);
        EXPECT_EQ(res.hit, true);
        EXPECT_EQ(*std::any_cast<std::string>(res.hitpayload), std::string("at 1000 0 0"));
    }

    {
        hitresult_t res = si.trace_ray(qvec3f(0, 0, 100), qvec3f(0, 0, -1));

        EXPECT_NEAR(res.hitpos[0], 0, epsilon);
        EXPECT_NEAR(res.hitpos[1], 0, epsilon);
        EXPECT_NEAR(res.hitpos[2], 0, epsilon);
        EXPECT_EQ(res.hit, true);
        EXPECT_EQ(*std::any_cast<std::string>(res.hitpayload), std::string("at 0 0 0"));
    }

    {
        hitresult_t res = si.trace_ray(qvec3f(500, 0, 100), qvec3f(0, 0, -1));

        EXPECT_EQ(res.hit, false);
        EXPECT_EQ(res.hitpayload, nullptr);
    }

    si.clear();
    si.commit();

    {
        hitresult_t res = si.trace_ray(qvec3f(0, 0, 100), qvec3f(0, 0, -1));

        EXPECT_EQ(res.hit, false);
        EXPECT_EQ(res.hitpayload, nullptr);
    }

    si.clear();
    si.add_poly(make_winding(qvec3d(500,0,0)), std::string("at 500 0 0"));
    si.commit();

    {
        hitresult_t res = si.trace_ray(qvec3f(500, 0, 100), qvec3f(0, 0, -1));

        EXPECT_NEAR(res.hitpos[0], 500, epsilon);
        EXPECT_NEAR(res.hitpos[1], 0, epsilon);
        EXPECT_NEAR(res.hitpos[2], 0, epsilon);
        EXPECT_EQ(res.hit, true);
        EXPECT_EQ(*std::any_cast<std::string>(res.hitpayload), std::string("at 500 0 0"));
    }
}
