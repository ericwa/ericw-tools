## ericw-tools
 - Website:         http://ericwa.github.io/ericw-tools
 - Documentation:
   - 2.0.0-alpha: https://ericw-tools.readthedocs.io
   - 0.18: [qbsp](https://ericwa.github.io/ericw-tools/doc/qbsp.html), [vis](https://ericwa.github.io/ericw-tools/doc/vis.html), [light](https://ericwa.github.io/ericw-tools/doc/light.html), [bspinfo](https://ericwa.github.io/ericw-tools/doc/bspinfo.html), [bsputil](https://ericwa.github.io/ericw-tools/doc/bsputil.html)
 - Maintainer:      Eric Wasylishen (AKA ericw)
 - Email:           ewasylishen@gmail.com

### Original tyurtils:

 - Website: http://disenchant.net
 - Author:  Kevin Shanahan (AKA Tyrann)
 - Email:   tyrann@disenchant.net

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

Required dependencies:
- [Embree 4](https://github.com/RenderKit/embree)
- [oneTBB](https://github.com/uxlfoundation/oneTBB)

Optional dependencies:
- Python, Sphinx (for building manuals)
- Qt 6 (for `lightpreview` GUI)

Bundled dependencies:
- [fmt](https://github.com/fmtlib/fmt)
- [jsoncpp](https://github.com/open-source-parsers/jsoncpp)
- [nanobench](https://github.com/martinus/nanobench)
- [pareto](https://github.com/alandefreitas/pareto)
- [GoogleTest](https://github.com/google/googletest)
- [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h)
- [stb_image_write](https://github.com/nothings/stb/blob/master/stb_image_write.h)

### Ubuntu 24.04

NOTE: Builds using Ubuntu's embree packages produce a significantly slower `light` (i.e. over twice as slow) than ones released on Embree's GitHub. See `build-linux-64.sh` for a better method. 

```bash
sudo apt update
sudo apt install libembree-dev libtbb-dev cmake build-essential g++ qt6-base-dev
git clone --recursive https://github.com/ericwa/ericw-tools
cd ericw-tools
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j 8

# run tests
./tests/tests

# print qbsp help
./qbsp/qbsp --help

# launch lightpreview gui
./lightpreview/lightpreview
```

### Windows, obtaining required dependencies via vcpkg

Open a `cmd` window. First, obtain vcpkg and build the dependencies:

```bat
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe install embree tbb
cd ..
```

Next, clone the ericw-tools git repository + submodules:

```bat
git clone --recursive https://github.com/ericwa/ericw-tools
```

Open the `ericw-tools` folder in VS2022 (or higher) as a CMake project.

Go to "Project -> CMake Settings". Under "CMake Toolchain File", press the "..." button and browse to `vcpkg\scripts\buildsystems\vcpkg.cmake`. Then press "Save" to save your CMakeSettings.json.

Once CMake finishes, you should be able to select e.g. `qbsp.exe (qbsp\qbsp.exe)` in the "Select Startup Item" dropdown in the toolbar. (I had to restart VS).

#### IDE Tips - CLion

- Modify the "Google Test" run/debug configuration template to have `--gtest_catch_exceptions=0`, otherwise the  
  debugger doesn't stop on exceptions (segfaults etc.)

  (see: https://youtrack.jetbrains.com/issue/CPP-29559/Clion-LLDB-does-not-break-on-SEH-exceptions-within-GTest)

### macOS 10.15+

```
brew install embree tbb qt@6 cmake
python3 -m pip install sphinx_rtd_theme
git clone --recursive https://github.com/ericwa/ericw-tools
cd ericw-tools
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix embree);$(brew --prefix tbb)" -DCMAKE_BUILD_TYPE=Release
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

Builds using Embree are licensed under GPLv3+ for compatibility with the
Apache license.
