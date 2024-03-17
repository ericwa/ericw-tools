# Download embree and tbb
Invoke-WebRequest 'https://github.com/embree/embree/releases/download/v3.12.1/embree-3.12.1.x64.vc14.windows.zip' -OutFile 'embree64.zip'
7z x embree64.zip -oc:\
Invoke-WebRequest 'https://github.com/oneapi-src/oneTBB/releases/download/v2020.2/tbb-2020.2-win.zip' -OutFile 'tbb.zip'
7z x tbb.zip -oc:\

git submodule update --init --recursive

# Create and activate a Python virtual environment, and install sphinx (for building our documentation)
py -m venv ericwtools-env
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
ericwtools-env\Scripts\Activate.ps1
python.exe -m pip install -r docs/requirements.txt --force-reinstall

# Confirm Sphinx is installed
get-command sphinx-build

choco install ninja

mkdir build-windows
cd build-windows

cmake .. -DCMAKE_TOOLCHAIN_FILE="$Env:VCPKG_CMAKE_FILE" -GNinja -Dembree_DIR="C:\embree-3.12.1.x64.vc14.windows" -DTBB_DIR="C:\tbb\cmake" -DCMAKE_BUILD_TYPE=Release -DENABLE_LIGHTPREVIEW=YES -DQt5Widgets_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5Widgets"

ninja package
if ( $? -eq $false ) {
  throw "build failed"
}

.\tests\tests.exe --no-skip

if ( $? -eq $false ) {
  throw "tests failed"
}
