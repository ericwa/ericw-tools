## ericw-tools
 - Website:         http://ericwa.github.io/ericw-tools
 - Maintainer:      Eric Wasylishen (AKA ericw)
 - Email:           ewasylishen@gmail.com

### Original tyurtils:

 - Website: http://disenchant.net
 - Author:  Kevin Shanahan (AKA Tyrann)
 - Email:   tyrann@disenchant.net

[![Build Status](https://travis-ci.org/ericwa/ericw-tools.svg?branch=master)](https://travis-ci.org/ericwa/ericw-tools)
[![Build status](https://ci.appveyor.com/api/projects/status/7lpdcy7l3e840u70?svg=true)](https://ci.appveyor.com/project/EricWasylishen/ericw-tools)

## About

ericw-tools is a branch of Tyrann's quake 1 tools, focused on
adding lighting features, mostly borrowed from q3map2. There are a few
bugfixes for qbsp as well. Original readme follows:

A collection of command line utilities for building Quake levels and working
with various Quake file formats. I need to work on the documentation a bit
more, but below are some brief descriptions of the tools.

Included utilities:

 - qbsp    - Used for turning a .map file into a playable .bsp file.

 - light   - Used for lighting a level after the bsp stage. This util was previously known as TyrLite

 - vis     - Creates the potentially visible set (PVS) for a bsp.

 - bspinfo - Print stats about the data contained in a bsp file.

 - bsputil - Simple tool for manipulation of bsp file data

See the doc/ directory for more detailed descriptions of the various
tools capabilities.  See changelog.txt for a brief overview of recent
changes or https://github.com/ericwa/ericw-tools for the full changelog and
source code.

## Compiling

Requires CMake 2.8, groff, and a compiler with C99 and C++11 support.  
[Embree v2.10.0+](http://embree.github.io/) is optional but recommended.

Tested on:
 - Ubuntu 14.04 / Clang 3.5.0
 - Ubuntu 14.04 / gcc 4.8.4
 - OS X 10.11 / Xcode 7.3
 - Windows 10 / Visual Studio 2013 Community

### Ubuntu 14.04 x86_64

```
sudo apt-get install git cmake build-essential groff

git clone https://github.com/ericwa/ericw-tools
cd ericw-tools

mkdir build
cd build

wget https://github.com/embree/embree/releases/download/v2.14.0/embree-2.14.0.x86_64.linux.tar.gz -O embree.tgz
tar xf embree.tgz

cmake .. -DCMAKE_BUILD_TYPE=Release -Dembree_DIR="$(pwd)/embree-2.14.0.x86_64.linux/lib/cmake/embree-2.14.0"
make -j8 VERBOSE=1
```

Executables will be located in:

 - `ericw-tools/build/qbsp/qbsp`
 - `ericw-tools/build/vis/vis`
 - `ericw-tools/build/light/light`
 - `ericw-tools/build/bspinfo/bspinfo`
 - `ericw-tools/build/bsputil/bsputil`

## Credits

- Kevin Shanahan (AKA Tyrann) for the original [tyrutils](http://disenchant.net/utils)
- id Software (original release of these tools is at https://github.com/id-Software/quake-tools) 
- rebb (ambient occlusion, qbsp improvements)
- q3map2 authors (AO, sunlight2, penumbra, deviance are from [q3map2](https://github.com/TTimo/GtkRadiant/tree/master/tools/quake3/q3map2))
- Spike (hexen 2 support, phong shading, various features)
- MH (surface lights based on MHColour)
- mfx, sock, Lunaran (testing)
- Thanks to users at [func_msgboard](http://www.celephais.net/board/forum.php) for feedback and testing

## License

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Builds using Embree are licensed under GPLv3+ for compatibility with the
Apache license.
