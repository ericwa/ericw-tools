## tyrutils-ericw (v0.15.4)
 - Website:         http://ericwa.github.io/tyrutils-ericw
 - Maintainer:      Eric Wasylishen (AKA ericw)
 - Email:           ewasylishen@gmail.com

### Original tyurtils:

 - Website: http://disenchant.net
 - Author:  Kevin Shanahan (AKA Tyrann)
 - Email:   tyrann@disenchant.net

[![Build Status](https://travis-ci.org/ericwa/tyrutils-ericw.svg?branch=master)](https://travis-ci.org/ericwa/tyrutils-ericw)
[![Build status](https://ci.appveyor.com/api/projects/status/7lpdcy7l3e840u70?svg=true)](https://ci.appveyor.com/project/EricWasylishen/tyrutils-ericw)

## About

tyrutils-ericw is a branch of Tyrann's quake 1 tools, focused on
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
changes or https://github.com/ericwa/tyrutils-ericw for the full changelog and
source code.

## Compiling

Requires CMake and a compiler with C99 and C++11 support.  

Tested on:
 - Ubuntu 14.04 / Clang 3.5.0
 - Ubuntu 14.04 / gcc 4.8.4
 - OS X 10.11 / Xcode 7.3
 - Windows 10 / Visual Studio 2015 Community

Typical Unix build command:
```
mkdir build
cd build
cmake ..
make
```
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
