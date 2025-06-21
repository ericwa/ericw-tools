# Download embree and tbb
Invoke-WebRequest 'https://github.com/RenderKit/embree/releases/download/v4.4.0/embree-4.4.0.x64.windows.zip' -OutFile 'embree64.zip'
Expand-Archive -Path 'embree64.zip' -DestinationPath 'C:\embree-4.4.0'
Invoke-WebRequest 'https://github.com/uxlfoundation/oneTBB/releases/download/v2021.11.0/oneapi-tbb-2021.11.0-win.zip' -OutFile 'tbb.zip'
Expand-Archive -Path 'tbb.zip' -DestinationPath 'C:\'

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

cmake .. -GNinja -Dembree_DIR="C:\embree-4.4.0\lib\cmake\embree-4.4.0" -DTBB_DIR="C:\oneapi-tbb-2021.11.0\lib\cmake\tbb" -DCMAKE_BUILD_TYPE=Release -DENABLE_LIGHTPREVIEW=YES -DQt5Widgets_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5Widgets"

ninja package
if ( $? -eq $false ) {
  throw "build failed"
}

.\tests\tests.exe

if ( $? -eq $false ) {
  throw "tests failed"
}
