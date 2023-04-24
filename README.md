## ericw-tools
 - Website:         http://ericwa.github.io/ericw-tools
 - Maintainer:      Eric Wasylishen (AKA ericw)
 - Email:           ewasylishen@gmail.com

### Original tyurtils:

 - Website: http://disenchant.net
 - Author:  Kevin Shanahan (AKA Tyrann)
 - Email:   tyrann@disenchant.net

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
tools capabilities.  See changelog.md for a brief overview of recent
changes or https://github.com/ericwa/ericw-tools for the full changelog and
source code.

## Compiling

Dependencies: Embree 3.0+, TBB (TODO: version?), Sphinx (for building manuals)

### Ubuntu

NOTE: Builds using Ubuntu's embree packages produce a significantly slower `light` (i.e. over twice as slow) than ones released on Embree's GitHub. See `build-linux-64.sh` for a better method. 

```
sudo apt install libembree-dev libtbb-dev cmake build-essential g++
sudo apt install python3-pip
python3 -m pip install sphinx_rtd_theme
export PATH="~/.local/bin/:$PATH"
git clone --recursive https://github.com/ericwa/ericw-tools
cd ericw-tools
mkdir build
cd build
cmake ..
```

### Windows

Example using vcpkg (32-bit build):

```
git clone --recursive https://github.com/ericwa/ericw-tools
cd ericw-tools

# creates a python virtual environment in the directory `sphinx-venv`
# and install sphinx (for building the docs)
py.exe -m venv sphinx-venv
.\sphinx-venv\Scripts\Activate.ps1
py.exe -m pip install -r docs/requirements.txt

git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat

# NOTE: vcpkg builds for 32-bit by default
# NOTE: takes 30+ minutes
.\vcpkg\vcpkg install embree3
mkdir build
cd build

# PowerShell syntax for getting current directory -
# otherwise, replace with absolute path to "vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$(pwd)/../vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_GENERATOR_PLATFORM=Win32 -DSPHINX_EXECUTABLE="$(pwd)/../sphinx-venv/Scripts/sphinx-build.exe"
```

### macOS 10.15

```
brew install embree tbb
python3 -m pip install sphinx_rtd_theme
git clone --recursive https://github.com/ericwa/ericw-tools
cd ericw-tools
mkdir build
cd build
cmake .. -GXcode -DCMAKE_PREFIX_PATH="$(brew --prefix embree);$(brew --prefix tbb)"
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

Builds using Embree are licensed under GPLv3+ for compatibility with the
Apache license.
