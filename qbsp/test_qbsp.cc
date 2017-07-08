#include "gtest/gtest.h"

#include <qbsp/qbsp.hh>
#include <qbsp/map.hh>

// FIXME: Clear global data (planes, etc) between each test

static face_t *Brush_FirstFaceWithTextureName(brush_t *brush, const char *texname) {
    for (face_t *face = brush->faces; face; face = face->next) {
        if (map.texinfoTextureName(face->texinfo) == texname)
            return face;
    }
    return nullptr;
}

// https://github.com/ericwa/tyrutils-ericw/issues/158
TEST(qbsp, testTextureIssue) {
    const char *buf = R"(
{
    "classname" "worldspawn"
    "wad" "Q.wad"
    {
        ( -104 -4 23.999998 ) ( -96.000252 -4 39.999489 ) ( -96.000252 4 39.999489 ) skip 0 0 0 1.000000 1.000000 0 0 0
        ( -135.996902 4 80.001549 ) ( -152 4 72 ) ( -104 4 23.999998 ) skip 0 -11 -45 1.000000 -1.070000 0 0 0
        ( -152 -4 72 ) ( -135.996902 -4 80.001549 ) ( -95.998451 -4 40.003094 ) skip 0 -11 -45 1.000000 -1.070000 0 0 0
        ( -96.000633 -4 40.000637 ) ( -136 -4 80.000008 ) ( -136 4 80.000008 ) skip 0 0 0 1.000000 1.000000 0 0 0
        ( -136 -4 80 ) ( -152 -4 72 ) ( -152 4 72 ) skip 0 0 0 1.000000 1.000000 0 0 0
        ( -152 -4 72.000008 ) ( -104.000168 -4 24.000172 ) ( -104.000168 4 24.000172 ) tech02_1 0 -8 0 1.000000 0.750000 0 0 0
    }
}
)";
    
    parser_t parser;
    ParserInit(&parser, buf);
    
    mapentity_t worldspawn;
    // FIXME: adds the brush to the global map...
    ASSERT_TRUE(ParseEntity(&parser, &worldspawn));
    
    brush_t *brush = LoadBrush(&worldspawn.mapbrush(0), vec3_origin, 0);
    ASSERT_NE(nullptr, brush);
    
    face_t *face = Brush_FirstFaceWithTextureName(brush, "tech02_1");
    ASSERT_NE(nullptr, face);
    const mtexinfo_t &texinfo = map.mtexinfos.at(face->texinfo);
    
    for (int i=0; i<2; i++) {
        printf ("%f %f %f %f\n",
                texinfo.vecs[i][0],
                texinfo.vecs[i][1],
                texinfo.vecs[i][2],
                texinfo.vecs[i][3]);
    }
}

