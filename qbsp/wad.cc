/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <string.h>
#include <string>

#include <qbsp/qbsp.hh>
#include <qbsp/wad.hh>

static void WADList_LoadTextures(const wad_t *wadlist, dmiptexlump_t *lump);
static int WAD_LoadLump(const wad_t *wad, const char *name, uint8_t *dest);
static void WADList_AddAnimationFrames(wad_t *wadlist);

static texture_t *textures;

uint8_t thepalette[768] = // Quake palette
{
    0,0,0,15,15,15,31,31,31,47,47,47,63,63,63,75,75,75,91,91,91,107,107,107,123,123,123,139,139,139,155,155,155,171,171,171,187,187,187,203,203,203,219,219,219,235,235,235,15,11,7,23,15,11,31,23,11,39,27,15,47,35,19,55,43,23,63,47,23,75,55,27,83,59,27,91,67,31,99,75,31,107,83,31,115,87,31,123,95,35,131,103,35,143,111,35,11,11,15,19,19,27,27,27,39,39,39,51,47,47,63,55,55,75,63,63,87,71,71,103,79,79,115,91,91,127,99,99,
    139,107,107,151,115,115,163,123,123,175,131,131,187,139,139,203,0,0,0,7,7,0,11,11,0,19,19,0,27,27,0,35,35,0,43,43,7,47,47,7,55,55,7,63,63,7,71,71,7,75,75,11,83,83,11,91,91,11,99,99,11,107,107,15,7,0,0,15,0,0,23,0,0,31,0,0,39,0,0,47,0,0,55,0,0,63,0,0,71,0,0,79,0,0,87,0,0,95,0,0,103,0,0,111,0,0,119,0,0,127,0,0,19,19,0,27,27,0,35,35,0,47,43,0,55,47,0,67,
    55,0,75,59,7,87,67,7,95,71,7,107,75,11,119,83,15,131,87,19,139,91,19,151,95,27,163,99,31,175,103,35,35,19,7,47,23,11,59,31,15,75,35,19,87,43,23,99,47,31,115,55,35,127,59,43,143,67,51,159,79,51,175,99,47,191,119,47,207,143,43,223,171,39,239,203,31,255,243,27,11,7,0,27,19,0,43,35,15,55,43,19,71,51,27,83,55,35,99,63,43,111,71,51,127,83,63,139,95,71,155,107,83,167,123,95,183,135,107,195,147,123,211,163,139,227,179,151,
    171,139,163,159,127,151,147,115,135,139,103,123,127,91,111,119,83,99,107,75,87,95,63,75,87,55,67,75,47,55,67,39,47,55,31,35,43,23,27,35,19,19,23,11,11,15,7,7,187,115,159,175,107,143,163,95,131,151,87,119,139,79,107,127,75,95,115,67,83,107,59,75,95,51,63,83,43,55,71,35,43,59,31,35,47,23,27,35,19,19,23,11,11,15,7,7,219,195,187,203,179,167,191,163,155,175,151,139,163,135,123,151,123,111,135,111,95,123,99,83,107,87,71,95,75,59,83,63,
    51,67,51,39,55,43,31,39,31,23,27,19,15,15,11,7,111,131,123,103,123,111,95,115,103,87,107,95,79,99,87,71,91,79,63,83,71,55,75,63,47,67,55,43,59,47,35,51,39,31,43,31,23,35,23,15,27,19,11,19,11,7,11,7,255,243,27,239,223,23,219,203,19,203,183,15,187,167,15,171,151,11,155,131,7,139,115,7,123,99,7,107,83,0,91,71,0,75,55,0,59,43,0,43,31,0,27,15,0,11,7,0,0,0,255,11,11,239,19,19,223,27,27,207,35,35,191,43,
    43,175,47,47,159,47,47,143,47,47,127,47,47,111,47,47,95,43,43,79,35,35,63,27,27,47,19,19,31,11,11,15,43,0,0,59,0,0,75,7,0,95,7,0,111,15,0,127,23,7,147,31,7,163,39,11,183,51,15,195,75,27,207,99,43,219,127,59,227,151,79,231,171,95,239,191,119,247,211,139,167,123,59,183,155,55,199,195,55,231,227,87,127,191,255,171,231,255,215,255,255,103,0,0,139,0,0,179,0,0,215,0,0,255,0,0,255,243,147,255,247,199,255,255,255,159,91,83
};

static bool
StringEndsWith(const std::string &gah, const char *woo)
{
    size_t l = strlen(woo);
    size_t gl = gah.length();
    if (gl < l)
        return false;
    gl-=l;
    while(*woo) {
        if (tolower(gah.at(gl++)) != *woo++)
            return false;
    }
    return true;
}



//Spike: Basic dds support, with a limited number of pixel formats supported.
typedef struct {
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwFourCC;

	unsigned int bitcount;
	unsigned int redmask;
	unsigned int greenmask;
	unsigned int bluemask;
	unsigned int alphamask;
} ddspixelformat_t;

typedef struct {
	unsigned int dwSize;
	unsigned int dwFlags;
	unsigned int dwHeight;
	unsigned int dwWidth;
	unsigned int dwPitchOrLinearSize;
	unsigned int dwDepth;
	unsigned int dwMipMapCount;
	unsigned int dwReserved1[11];
	ddspixelformat_t ddpfPixelFormat;
	unsigned int ddsCaps[4];
	unsigned int dwReserved2;
} ddsheader_t;
typedef struct {
	unsigned int dxgiformat;
	unsigned int resourcetype; //0=unknown, 1=buffer, 2=1d, 3=2d, 4=3d
	unsigned int miscflag;	//singular... yeah. 4=cubemap.
	unsigned int arraysize;
	unsigned int miscflags2;
} dds10header_t;

template <class t> inline t max (t a, t b) {return (a>b?a:b);}	//because macro double-expansion sucks

static size_t Image_ReadDDSFile(const char *fname, const char *mipname, uint8_t *filedata, size_t filesize, void **out)
{
    size_t lumpsize = 0;
    int nummips;
    int mipnum;
    int datasize;
    unsigned int w, h, d;
    unsigned int blockwidth, blockheight, blockdepth=1, blockbytes, inblockbytes=0;
    const char *encoding;
    int layers = 1;
    bool swap = false;

    ddsheader_t fmtheader;
    dds10header_t fmt10header;
    uint8_t *fileend = filedata + filesize;

    if (filesize < sizeof(fmtheader) || *(int*)filedata != (('D'<<0)|('D'<<8)|('S'<<16)|(' '<<24)))
        return 0;

    memcpy(&fmtheader, filedata+4, sizeof(fmtheader));
    if (fmtheader.dwSize != sizeof(fmtheader))
        return 0;	//corrupt/different version
    fmtheader.dwSize += 4;
    memset(&fmt10header, 0, sizeof(fmt10header));

    fmt10header.arraysize = (fmtheader.ddsCaps[1] & 0x200)?6:1; //cubemaps need 6 faces...

    nummips = fmtheader.dwMipMapCount;
    if (nummips < 1)
        nummips = 1;

    if (!(fmtheader.ddpfPixelFormat.dwFlags & 4))
    {
        #define IsPacked(bits,r,g,b,a)	fmtheader.ddpfPixelFormat.bitcount==bits&&fmtheader.ddpfPixelFormat.redmask==r&&fmtheader.ddpfPixelFormat.greenmask==g&&fmtheader.ddpfPixelFormat.bluemask==b&&fmtheader.ddpfPixelFormat.alphamask==a
        if (IsPacked(24, 0xff0000, 0x00ff00, 0x0000ff, 0))
            encoding = "RGB", blockbytes=3, blockwidth=blockheight=1, swap = true;
        else if (IsPacked(24, 0x000000ff, 0x0000ff00, 0x00ff0000, 0))
            encoding = "RGB", blockbytes=3, blockwidth=blockheight=1;
        else if (IsPacked(32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
            encoding = "RGBA", blockbytes=4, blockwidth=blockheight=1, swap = true;
        else if (IsPacked(32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
            encoding = "RGBA", blockbytes=4, blockwidth=blockheight=1;
        else if (IsPacked(32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0))
            encoding = "RGB", blockbytes=3, blockwidth=blockheight=1, swap=true, inblockbytes=4;
        else if (IsPacked(32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0))
            encoding = "RGB", blockbytes=3, blockwidth=blockheight=1, inblockbytes=4;
//      else if (IsPacked(32, 0x000003ff, 0x000ffc00, 0x3ff00000, 0xc0000000))
//          encoding = PTI_A2BGR10;
        else if (IsPacked(16, 0xf800, 0x07e0, 0x001f, 0))
            encoding = "565", blockbytes=2, blockwidth=blockheight=1;
        else if (IsPacked(16, 0xf800, 0x07c0, 0x003e, 0x0001))
            encoding = "5551", blockbytes=2, blockwidth=blockheight=1;
        else if (IsPacked(16, 0xf000, 0x0f00, 0x00f0, 0x000f))
            encoding = "4444", blockbytes=2, blockwidth=blockheight=1;
/*      else if (IsPacked( 8, 0x000000ff, 0x00000000, 0x00000000, 0x00000000))
            encoding = "LUM8";
        else if (IsPacked(16, 0x000000ff, 0x00000000, 0x00000000, 0x0000ff00))
            encoding = PTI_L8A8;
*/      else
        {
            logprint("Unsupported non-fourcc dds in %s\n", fname);
            logprint(" bits: %u\n", fmtheader.ddpfPixelFormat.bitcount);
            logprint("  red: %08x\n", fmtheader.ddpfPixelFormat.redmask);
            logprint("green: %08x\n", fmtheader.ddpfPixelFormat.greenmask);
            logprint(" blue: %08x\n", fmtheader.ddpfPixelFormat.bluemask);
            logprint("alpha: %08x\n", fmtheader.ddpfPixelFormat.alphamask);
            logprint(" used: %08x\n", fmtheader.ddpfPixelFormat.redmask^fmtheader.ddpfPixelFormat.greenmask^fmtheader.ddpfPixelFormat.bluemask^fmtheader.ddpfPixelFormat.alphamask);
            return 0;
        }
#undef IsPacked
    }
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('D'<<0)|('X'<<8)|('T'<<16)|('1'<<24)))
        encoding = "BC1", blockbytes=8, blockwidth=blockheight=4;	//alpha or not? Assume yes, and let the drivers decide.
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('D'<<0)|('X'<<8)|('T'<<16)|('2'<<24)))	//dx3 with premultiplied alpha
        encoding = "BC2", blockbytes=16, blockwidth=blockheight=4;
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('D'<<0)|('X'<<8)|('T'<<16)|('3'<<24)))
        encoding = "BC2", blockbytes=16, blockwidth=blockheight=4;
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('D'<<0)|('X'<<8)|('T'<<16)|('4'<<24)))	//dx5 with premultiplied alpha
        encoding = "BC3", blockbytes=16, blockwidth=blockheight=4;
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('D'<<0)|('X'<<8)|('T'<<16)|('5'<<24)))
        encoding = "BC3", blockbytes=16, blockwidth=blockheight=4;
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('E'<<0)|('T'<<8)|('C'<<16)|('2'<<24)))
        encoding = "ETC2", blockbytes=8, blockwidth=blockheight=4;
    else if (*(int*)&fmtheader.ddpfPixelFormat.dwFourCC == (('D'<<0)|('X'<<8)|('1'<<16)|('0'<<24)))
    {
        //this has some weird extra header with dxgi format types.
        memcpy(&fmt10header, filedata+fmtheader.dwSize, sizeof(fmt10header));
        fmtheader.dwSize += sizeof(fmt10header);
        switch(fmt10header.dxgiformat)
        {
        //note: we don't distinguish between ldr+srgb formats here... they might end up darker than intended...
        case 0x1c/*DXGI_FORMAT_R8G8B8A8_UNORM*/:
        case 0x1d/*DXGI_FORMAT_R8G8B8A8_UNORM_SRGB*/:	encoding = "RGBA", blockbytes=4, blockwidth=1, blockheight=1; break;   //32bit
        case 0x43/*DXGI_FORMAT_R9G9B9E5_SHAREDEXP*/:	encoding = "EXP5", blockbytes=4, blockwidth=1, blockheight=1; break;   //32bit
        case 0x47/*DXGI_FORMAT_BC1_UNORM*/:
        case 0x48/*DXGI_FORMAT_BC1_UNORM_SRGB*/:	encoding = "BC1", blockbytes=8,  blockwidth=4, blockheight=4; break;   //4 bit
        case 0x4a/*DXGI_FORMAT_BC2_UNORM*/:
        case 0x4b/*DXGI_FORMAT_BC2_UNORM_SRGB*/:	encoding = "BC2", blockbytes=16, blockwidth=4, blockheight=4; break;   //8 bit
        case 0x4d/*DXGI_FORMAT_BC3_UNORM*/:
        case 0x4e/*DXGI_FORMAT_BC3_UNORM_SRGB*/:	encoding = "BC3", blockbytes=16, blockwidth=4, blockheight=4; break;   //8 bit
        case 0x50/*DXGI_FORMAT_BC4_UNORM*/:		encoding = "BC4", blockbytes=8,  blockwidth=4, blockheight=4; break;   //4 bit
        case 0x53/*DXGI_FORMAT_BC5_UNORM*/:		encoding = "BC5", blockbytes=16, blockwidth=4, blockheight=4; break;   //8 bit
        case 0x5f/*DXGI_FORMAT_BC6H_UF16*/:		encoding = "BC6", blockbytes=16, blockwidth=4, blockheight=4; break;   //8 bit
        case 0x62/*DXGI_FORMAT_BC7_UNORM*/:
        case 0x63/*DXGI_FORMAT_BC7_UNORM_SRGB*/:	encoding = "BC7", blockbytes=16, blockwidth=4, blockheight=4; break;   //8 bit
        case 134:
        case 135:	encoding = "AST4", blockbytes=16, blockwidth=4, blockheight=4; break;   //8   bit
        case 138:
        case 139:	encoding = "AS54", blockbytes=16, blockwidth=5, blockheight=4; break;   //6.4 bit
        case 142:
        case 143:	encoding = "AST5", blockbytes=16, blockwidth=5, blockheight=5; break;   //5.12bit
        case 146:
        case 147:	encoding = "AS65", blockbytes=16, blockwidth=6, blockheight=5; break;   //4.27bit
        case 150:
        case 151:	encoding = "AST6", blockbytes=16, blockwidth=6, blockheight=6; break;   //3.55bit
        case 154:
        case 155:	encoding = "AS85", blockbytes=16, blockwidth=8, blockheight=5; break;   //3.2 bit
        case 158:
        case 159:	encoding = "AS86", blockbytes=16, blockwidth=8, blockheight=6; break;   //2.67bit
        case 162:
        case 163:	encoding = "AST8", blockbytes=16, blockwidth=8, blockheight=8; break;   //2   bit
        case 166:
        case 167:	encoding = "AS05", blockbytes=16, blockwidth=10, blockheight=5; break;  //2.56bit
        case 170:
        case 171:	encoding = "AS06", blockbytes=16, blockwidth=10, blockheight=6; break;  //2.13bit
        case 174:
        case 175:	encoding = "AS08", blockbytes=16, blockwidth=10, blockheight=8; break;  //1.6 bit
        case 178:
        case 179:	encoding = "AST0", blockbytes=16, blockwidth=10, blockheight=10; break; //1.28bit
        case 182:
        case 183:	encoding = "AS20", blockbytes=16, blockwidth=12, blockheight=10; break; //1.07bit
        case 186:
        case 187:	encoding = "AST2", blockbytes=16, blockwidth=12, blockheight=12; break; //0.88bit
        default:
            logprint("Unsupported dds10 dxgi in %s - %u\n", fname, fmt10header.dxgiformat);
            return 0;
        }
    }
    else
    {
        logprint("Unsupported dds fourcc in %s - \"%c%c%c%c\"\n", fname,
        ((char*)&fmtheader.ddpfPixelFormat.dwFourCC)[0],
        ((char*)&fmtheader.ddpfPixelFormat.dwFourCC)[1],
        ((char*)&fmtheader.ddpfPixelFormat.dwFourCC)[2],
        ((char*)&fmtheader.ddpfPixelFormat.dwFourCC)[3]);
        return 0;
    }
    if (!inblockbytes)
        inblockbytes = blockbytes;

    if ((fmtheader.ddsCaps[1] & 0x200) && (fmtheader.ddsCaps[1] & 0xfc00) != 0xfc00)
        return 0;	//cubemap without all 6 faces defined.

    if (fmtheader.dwFlags & 8)
    {	//explicit pitch flag. we don't support any padding, so this check exists just to be sure none is required.
        w = max(1u, fmtheader.dwWidth);
        if (fmtheader.dwPitchOrLinearSize != inblockbytes*(w+blockwidth-1)/blockwidth)
            return 0;
    }
    if (fmtheader.dwFlags & 0x80000)
    {	//linear size flag. we don't support any padding, so this check exists just to be sure none is required.
        //linear-size of the top-level mip.
        size_t linearsize;
        w = max(1u, fmtheader.dwWidth);
        h = max(1u, fmtheader.dwHeight);
        d = max(1u, fmtheader.dwDepth);
        linearsize = ((w+blockwidth-1)/blockwidth)*
                                        ((h+blockheight-1)/blockheight)*
                                        ((d+blockdepth-1)/blockdepth)*
                                        inblockbytes;
        if (fmtheader.dwPitchOrLinearSize != linearsize)
            return 0;
    }

    if (fmtheader.ddsCaps[1] & 0x200)
        return 0;    //no cubemaps stuff
    else if (fmtheader.ddsCaps[1] & 0x200000)
        return 0;	//no 3d arrays
    else
    {
        if (fmt10header.arraysize != 1)
            return 0;    //no array textures
    }

    filedata += fmtheader.dwSize;

    w = max(1u, fmtheader.dwWidth);
    h = max(1u, fmtheader.dwHeight);
    d = max(1u, fmtheader.dwDepth);

    lumpsize = sizeof(dmiptex_t);
    lumpsize += 20;
    for (mipnum = 0; mipnum < nummips; mipnum++)
    {
        lumpsize += ((w+blockwidth-1)/blockwidth) * ((h+blockheight-1)/blockheight) * ((d+blockdepth-1)/blockdepth) * blockbytes;
        w = max(1u, w>>1);
        h = max(1u, h>>1);
        d = max(1u, d>>1);
    }


    w = max(1u, fmtheader.dwWidth);
    h = max(1u, fmtheader.dwHeight);
    d = max(1u, fmtheader.dwDepth);

    auto outdata = (uint8_t*)AllocMem(OTHER, lumpsize, false);
    *out = outdata;

    //spit out the mip header
    {
        auto mt = (dmiptex_t*)outdata;
        q_snprintf(mt->name, sizeof(mt->name), "%s", mipname);
        mt->width = w;
        mt->height = h;
        //no paletted data. sue me.
        mt->offsets[0] = mt->offsets[1] = mt->offsets[2] = mt->offsets[3] = 0;
    } outdata += sizeof(dmiptex_t);

    //magic ident, extsize, pixelformat, w, h.
    outdata[0]=0x00;
    outdata[1]=0xfb;
    outdata[2]=0x2b;
    outdata[3]=0xaf;	outdata+=sizeof(int);
    //extsize (little-endian)
    *(int*)outdata = lumpsize-(outdata-(uint8_t*)*out); outdata+=sizeof(int);
    //pixel format (fourcc)
    outdata[0]=encoding[0];
    outdata[1]=encoding[1];
    outdata[2]=encoding[2];
    outdata[3]=encoding[3];	outdata+=sizeof(int);
    //width (little-endian)
    *(int*)outdata = w;	outdata+=sizeof(int);
    //height (little-endian)
    *(int*)outdata = h;	outdata+=sizeof(int);

    //now the mip chain.
    //Note: dds groups by layer rather than level. we don't do cubemaps or arrays so w/e.
    for (mipnum = 0; mipnum < nummips; mipnum++)
    {
        datasize = ((w+blockwidth-1)/blockwidth) * ((h+blockheight-1)/blockheight) * (layers*((d+blockdepth-1)/blockdepth)) * blockbytes;
        if (swap || blockbytes != inblockbytes)
        {
            for (size_t p = 0; p < datasize/blockbytes; p++, filedata += inblockbytes, outdata += blockbytes)
            {
                if (swap)
                {   //bgr->rgb
                    outdata[0] = filedata[2];
                    outdata[1] = filedata[1];
                    outdata[2] = filedata[0];
                }
                else
                {   //just stripping padding.
                    outdata[0] = filedata[0];
                    outdata[1] = filedata[1];
                    outdata[2] = filedata[2];
                }
                if (blockbytes > 3)
                    outdata[3] = filedata[3];
            }
        }
        else
        {
            memcpy(outdata, filedata, datasize);
            filedata += datasize;
            outdata += datasize;
        }
        w = max(1u, w>>1);
        h = max(1u, h>>1);
        d = max(1u, d>>1);
    }

    return lumpsize;
}

static bool
WADList_LoadImageFile(const char *fname, const char *texname, wad_t *&current_wadlist)
{
    auto f = fopen(fname, "rb");
    if (!f)
        return false; //nope no file.
    if (options.fVerbose)
        Message(msgLiteral, "Reading image: %s\n", fname);
    fseek(f, 0l, SEEK_END);
    size_t filesize = ftell(f);
    fseek(f, 0l, SEEK_SET);
    mlumpinfo_t l = {};
    auto filedata = (uint8_t *)malloc(filesize);
    if (filesize != fread(filedata, 1, filesize, f))
        filesize = 0; //something went wrong...
    fclose(f);

    if (!l.mip)
        l.size = l.disksize = Image_ReadDDSFile(fname, texname, filedata, filesize, &l.mip);
    free(filedata); //no longer needed

    if (l.mip)
    {
        auto newwad = (wad_t *)AllocMem(OTHER, sizeof(wad_t), true);
        newwad->file = NULL;
        newwad->version = 2;
        newwad->header.numlumps = 1;
        newwad->lumps = (mlumpinfo_t *)AllocMem(OTHER, sizeof(*newwad->lumps), true);
        *newwad->lumps = l;
        memcpy(newwad->lumps->name, texname, sizeof(newwad->lumps->name));
        newwad->next = current_wadlist;
        current_wadlist = newwad;
        return true;
    }
    else if (options.fVerbose)
        Message(msgLiteral, "Unknown format: %s\n", fname);
    return false;
}

static bool
WADList_AddMip(const char *fpath, wad_t *&current_wadlist)
{
    std::string fullPath;
    wad_t wad = {0};

    if (options.wadPathsVec.empty() || IsAbsolutePath(fpath)) {
        fullPath = fpath;
        wad.file = fopen(fullPath.c_str(), "rb");
        if (!wad.file)
        {
            fullPath = std::string(fpath) + ".mip";
            wad.file = fopen(fullPath.c_str(), "rb");
        }
    } else {
        for (const options_t::wadpath& wadpath : options.wadPathsVec) {
            if (*fpath == '*')  //turbs are annoying
                fullPath = wadpath.path + "/#" + (fpath+1) + ".mip";
            else
                fullPath = wadpath.path + "/" + fpath + ".mip";
            wad.file = fopen(fullPath.c_str(), "rb");
            if (wad.file)
                break;

            if (*fpath == '*')  //turbs are annoying
                fullPath = wadpath.path + "/#" + (fpath+1) + ".dds";
            else
                fullPath = wadpath.path + "/" + fpath + ".dds";
            if (WADList_LoadImageFile(fullPath.c_str(), fpath, current_wadlist))
                return true;

            char *lpath = strdup(fpath);
            bool lowered = false;
            for (auto p = lpath; *p; p++)
                if (*p >= 'A' && *p <= 'Z')
                    *p += 'a'-'A', lowered = true;
            if (lowered)
            {
                if (*lpath == '*')  //turbs are annoying
                    fullPath = wadpath.path + "/#" + (lpath+1) + ".dds";
                else
                    fullPath = wadpath.path + "/" + lpath + ".dds";
                if (WADList_LoadImageFile(fullPath.c_str(), fpath, current_wadlist))
                {
                    free(lpath);
                    return true;
                }
            }
            free(lpath);
        }
    }

    if (wad.file && StringEndsWith(fullPath, ".mip")) {
        if (options.fVerbose)
            Message(msgLiteral, "Opened MIP: %s\n", fullPath.c_str());
        wad.version = 2;
        wad.header.numlumps = 1;
        wad.lumps = (mlumpinfo_t *)AllocMem(OTHER, sizeof(*wad.lumps), true);

        dmiptex_t miptex;
        fread(&miptex, 1, sizeof(miptex), wad.file);
        memcpy(wad.lumps->name, fpath, sizeof(wad.lumps->name));
        wad.lumps->filepos = 0;
        fseek(wad.file, 0, SEEK_END);
        wad.lumps->size = wad.lumps->disksize = ftell(wad.file);

        auto tex = (texture_t *)AllocMem(OTHER, sizeof(texture_t), true);
        tex->next = textures;
        textures = tex;
        memcpy(tex->name, miptex.name, 16);
        tex->name[15] = '\0';
        tex->width = miptex.width;
        tex->height = miptex.height;


        wad_t *newwad = (wad_t *)AllocMem(OTHER, sizeof(wad), true);
        memcpy(newwad, &wad, sizeof(wad));
        newwad->next = current_wadlist;
        current_wadlist = newwad;

        return true;
    }
    if (wad.file)
        fclose(wad.file);
    return false;
}

static bool
WAD_LoadInfo(wad_t *wad, bool external)
{
    wadinfo_t *hdr = &wad->header;
    int i, len, lumpinfosize;
    dmiptex_t miptex;
    texture_t *tex;
    dlumpinfo_t *lumps;

    external |= options.fNoTextures;

    len = fread(hdr, 1, sizeof(wadinfo_t), wad->file);
    if (len != sizeof(wadinfo_t))
        return false;

    wad->version = 0;
    if (!strncmp(hdr->identification, "WAD2", 4))
        wad->version = 2;
    else if (!strncmp(hdr->identification, "WAD3", 4))
        wad->version = 3;
    if (!wad->version)
        return false;

    lumpinfosize = sizeof(dlumpinfo_t) * hdr->numlumps;
    fseek(wad->file, hdr->infotableofs, SEEK_SET);
    wad->lumps = (mlumpinfo_t *)AllocMem(OTHER, sizeof(mlumpinfo_t)*hdr->numlumps, true);
    lumps = (dlumpinfo_t *)AllocMem(OTHER, lumpinfosize, true);
    len = fread(wad->lumps, 1, lumpinfosize, wad->file);
    if (len != lumpinfosize)
        wad->header.numlumps = 0;

    /* Get the dimensions and make a texture_t */
    for (i = 0; i < wad->header.numlumps; i++) {
        wad->lumps[i].filepos = lumps[i].filepos;
        wad->lumps[i].disksize = lumps[i].disksize;
        wad->lumps[i].size = lumps[i].size;
        wad->lumps[i].mip = NULL;
        strncpy(wad->lumps[i].name, lumps[i].name, sizeof(lumps[i].name));
        wad->lumps[i].name[sizeof(lumps[i].name)] = 0;

        fseek(wad->file, wad->lumps[i].filepos, SEEK_SET);
        len = fread(&miptex, 1, sizeof(miptex), wad->file);

        if (len == sizeof(miptex))
        {
            unsigned int magic;
            int w = LittleLong(miptex.width);
            int h = LittleLong(miptex.height);
            int m;

            wad->lumps[i].size = sizeof(miptex);
            for (m = 0; m < 4 && miptex.offsets[m] && wad->lumps[i].size < LittleLong(miptex.offsets[m])+(w>>m)*(h>>m); m++)
                wad->lumps[i].size += (w>>m)*(h>>m);
            if (options.BSPVersion == BSPHLVERSION)
                wad->lumps[i].size += 2+3*256;    //palette size+palette data

            fseek(wad->file, wad->lumps[i].filepos+wad->lumps[i].size, SEEK_SET);
            fread(&magic, 1, sizeof(magic), wad->file);
            if (LittleLong(magic) == ((0x00<<8)|(0xfb<<8)|(0x2b<<16)|(0xafu<<24)) && !(wad->lumps[i].disksize&3))   //if there's extension data in there then just load it as the size its meant to be instead of messing stuff up. we don't know what's actually in there.
                wad->lumps[i].size = wad->lumps[i].disksize;

            wad->lumps[i].size = (wad->lumps[i].size+3) & ~3;    //keep things aligned if we can.

            tex = (texture_t *)AllocMem(OTHER, sizeof(texture_t), true);
            tex->next = textures;
            textures = tex;
            memcpy(tex->name, miptex.name, 16);
            tex->name[15] = '\0';
            tex->width = miptex.width;
            tex->height = miptex.height;

            //if we're not going to embed it into the bsp, set its size now so we know how much to actually store.
            if (external)
                wad->lumps[i].size = wad->lumps[i].disksize = sizeof(dmiptex_t);

            //printf("Created texture_t %s %d %d\n", tex->name, tex->width, tex->height);
        }
        else
            wad->lumps[i].size = 0;
    }
    FreeMem(lumps, OTHER, lumpinfosize);
    return wad->header.numlumps>0;
}

wad_t *WADList_AddWad(const char *fpath, bool external, wad_t *current_wadlist)
{
    wad_t wad = {0};
    
    wad.file = fopen(fpath, "rb");
    if (wad.file) {
        if (options.fVerbose)
            Message(msgLiteral, "Opened WAD: %s\n", fpath);
        if (WAD_LoadInfo(&wad, external)) {
            wad_t *newwad = (wad_t *)AllocMem(OTHER, sizeof(wad), true);
            memcpy(newwad, &wad, sizeof(wad));
            newwad->next = current_wadlist;
            
            // FIXME: leaves file open?
            // (currently needed so that mips can be loaded later, as needed)
            
            return newwad;
        } else {
            Message(msgWarning, warnNotWad, fpath);
            fclose(wad.file);
        }
    }
    return current_wadlist;
}

wad_t *
WADList_Init(const char *wadstring)
{
    if (!wadstring || !wadstring[0])
        return nullptr;

    wad_t *wadlist = nullptr;
    const int len = strlen(wadstring);
    const char *pos = wadstring;
    while (pos - wadstring < len) {
        // split string by ';' and copy the current component into fpath
        const char *const fname = pos;
        while (*pos && *pos != ';')
            pos++;

        const size_t fpathLen = pos - fname;
        std::string fpath;
        fpath.resize(fpathLen);
        memcpy(&fpath[0], fname, fpathLen);

        if (options.wadPathsVec.empty() || IsAbsolutePath(fpath.c_str())) {
            wadlist = WADList_AddWad(fpath.c_str(), false, wadlist);
        } else {
            for (const options_t::wadpath& wadpath : options.wadPathsVec) {
                const std::string fullPath = wadpath.path + "/" + fpath;
                wadlist = WADList_AddWad(fullPath.c_str(), wadpath.external, wadlist);
            }
        }

        pos++;
    }

    return wadlist;
}


void
WADList_Free(wad_t *wadlist)
{
    wad_t *wad, *next;

    for (wad = wadlist; wad; wad = next) {
        next = wad->next;
        fclose(wad->file);
        FreeMem(wad->lumps, OTHER, sizeof(mlumpinfo_t) * wad->header.numlumps);
        FreeMem(wad, OTHER, sizeof(*wad));
    }
}

static mlumpinfo_t *
WADList_FindTexture(wad_t *&wadlist, const char *name)
{
    int i;
    const wad_t *wad;

    for (wad = wadlist; wad; wad = wad->next)
        for (i = 0; i < wad->header.numlumps; i++)
            if (!Q_strcasecmp(name, wad->lumps[i].name))
                return &wad->lumps[i];

    if (WADList_AddMip(name, wadlist))
        return &wadlist->lumps[0];

    return NULL;
}

void
WADList_Process(wad_t *wadlist)
{
    int i;
    mlumpinfo_t *texture;
    dmiptexlump_t *miptexlump;
    struct lumpdata *texdata = &pWorldEnt()->lumps[LUMP_TEXTURES];

    WADList_AddAnimationFrames(wadlist);

    /* Count space for miptex header/offsets */
    texdata->count = offsetof(dmiptexlump_t, dataofs[0]) + (map.nummiptex() * sizeof(uint32_t));

    /* Count texture size.  Slower, but saves memory. */
    for (i = 0; i < map.nummiptex(); i++) {
        texture = WADList_FindTexture(wadlist, map.miptex.at(i).c_str());
        if (texture) {
            texdata->count += texture->size;
        }
    }

    /* Default texture data to store in worldmodel */
    texdata->data = AllocMem(BSP_TEX, texdata->count, true);
    miptexlump = (dmiptexlump_t *)texdata->data;
    miptexlump->nummiptex = map.nummiptex();

    WADList_LoadTextures(wadlist, miptexlump);

    /* Last pass, mark unfound textures as such */
    for (i = 0; i < map.nummiptex(); i++) {
        if (miptexlump->dataofs[i] == 0) {
            miptexlump->dataofs[i] = -1;
            Message(msgWarning, warnTextureNotFound, map.miptex.at(i).c_str());
        }
    }
}

static void
WADList_LoadTextures(const wad_t *wadlist, dmiptexlump_t *lump)
{
    int i, size;
    uint8_t *data;
    const wad_t *wad;
    struct lumpdata *texdata = &pWorldEnt()->lumps[LUMP_TEXTURES];

    data = (uint8_t *)&lump->dataofs[map.nummiptex()];

    for (i = 0; i < map.nummiptex(); i++) {
        if (lump->dataofs[i])
            continue;
        size = 0;
        for (wad = wadlist; wad; wad = wad->next) {
            size = WAD_LoadLump(wad, map.miptex.at(i).c_str(), data);
            if (size)
                break;
        }
        if (!size)
            continue;
        if (data + size - (uint8_t *)texdata->data > texdata->count)
            Error("Internal error: not enough texture memory allocated");
        lump->dataofs[i] = data - (uint8_t *)lump;
        data += size;
    }
}


static int
WAD_LoadLump(const wad_t *wad, const char *name, uint8_t *dest)
{
    int i;
    int size;

    for (i = 0; i < wad->header.numlumps; i++) {
        if (!Q_strcasecmp(name, wad->lumps[i].name)) {
            if (!wad->file)
            {   //pre-prepared mip.
                memcpy(dest, wad->lumps[i].mip, wad->lumps[i].disksize);
                return wad->lumps[i].disksize;
            }

            fseek(wad->file, wad->lumps[i].filepos, SEEK_SET);
            if (wad->lumps[i].disksize == sizeof(dmiptex_t))
            {
                size = fread(dest, 1, sizeof(dmiptex_t), wad->file);
                if (size != sizeof(dmiptex_t))
                    Error("Failure reading from file");
                for (i = 0; i < MIPLEVELS; i++)
                    ((dmiptex_t*)dest)->offsets[i] = 0;
                return sizeof(dmiptex_t);
            }

            if (wad->lumps[i].size != wad->lumps[i].disksize)
            {
                std::vector<uint8_t> data(wad->lumps[i].disksize);
                size = fread(data.data(), 1, wad->lumps[i].disksize, wad->file);
                if (size != wad->lumps[i].disksize)
                    Error("Failure reading from file");
                auto out = (dmiptex_t *)dest;
                auto in = (dmiptex_t *)data.data();
                *out = *in;
                out->offsets[0] = sizeof(*out);
                out->offsets[1] = out->offsets[0] + (in->width>>0)*(in->height>>0);
                out->offsets[2] = out->offsets[1] + (in->width>>1)*(in->height>>1);
                out->offsets[3] = out->offsets[2] + (in->width>>2)*(in->height>>2);
                auto palofs     = out->offsets[3] + (in->width>>3)*(in->height>>3);
                memcpy(dest+out->offsets[0], data.data()+(in->offsets[0]), (in->width>>0)*(in->height>>0));
                memcpy(dest+out->offsets[1], data.data()+(in->offsets[1]), (in->width>>1)*(in->height>>1));
                memcpy(dest+out->offsets[2], data.data()+(in->offsets[2]), (in->width>>2)*(in->height>>2));
                memcpy(dest+out->offsets[3], data.data()+(in->offsets[3]), (in->width>>3)*(in->height>>3));

                if (options.BSPVersion == BSPHLVERSION)
                {    //palette size. 256 in little endian.
                    dest[palofs+0] = ((256>>0)&0xff);
                    dest[palofs+1] = ((256>>8)&0xff);

                    //now the palette
                    if (wad->version == 3)
                        memcpy(dest+palofs+2, data.data()+(in->offsets[3]+(in->width>>3)*(in->height>>3)+2), 3*256);
                    else
                        memcpy(dest+palofs+2, thepalette, 3*256);    //FIXME: quake palette or something.
                }
            }
            else
            {
                size = fread(dest, 1, wad->lumps[i].disksize, wad->file);
                if (size != wad->lumps[i].disksize)
                    Error("Failure reading from file");
            }
            return wad->lumps[i].size;
        }
    }

    return 0;
}

static void
WADList_AddAnimationFrames(wad_t *wadlist)
{
    int oldcount, i, j;

    oldcount = map.nummiptex();

    for (i = 0; i < oldcount; i++) {
        if (map.miptex.at(i)[0] != '+' && (options.BSPVersion!=BSPHLVERSION||map.miptex.at(i)[0] != '-'))
            continue;
        std::string name = map.miptex.at(i);

        /* Search for all animations (0-9) and alt-animations (A-J) */
        for (j = 0; j < 20; j++) {
            name[1] = (j < 10) ? '0' + j : 'a' + j - 10;
            if (WADList_FindTexture(wadlist, name.c_str()))
                FindMiptex(name.c_str());
        }
    }

    Message(msgStat, "%8d texture frames added", map.nummiptex() - oldcount);
}

const texture_t *WADList_GetTexture(const char *name)
{
    texture_t *tex;
    for (tex = textures; tex; tex = tex->next)
    {
        if (!strcmp(name, tex->name))
            return tex;
    }
    return NULL;
}
