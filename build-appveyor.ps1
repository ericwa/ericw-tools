git submodule update --init --recursive

$env:Path += ";C:\cygwin64\bin"

# For sha256sum
$env:Path += ";C:\Program Files\Git\usr\bin"

# Create and activate a Python virtual environment, and install sphinx
py -m venv ericwtools-env
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
ericwtools-env\Scripts\Activate.ps1
python.exe -m pip install -r docs/requirements.txt --force-reinstall

# Confirm Sphinx is installed
get-command sphinx-build

mkdir cmakebuild

cd cmakebuild

cmake .. -T v143 -Dembree_DIR="C:\embree-3.12.1.x64.vc14.windows" -DTBB_DIR="C:\tbb\cmake" -DCMAKE_GENERATOR_PLATFORM=x64 -DENABLE_LIGHTPREVIEW=YES -DQt6Widgets_DIR="C:\Qt\6.7.0\msvc2019_64\lib\cmake\Qt6Widgets"

$cmakePlatform = "x64"

msbuild /p:Configuration=Release /p:Platform=$cmakePlatform /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll" PACKAGE.vcxproj

if ( $? -eq $false ) {
  throw "package failed"
}

.\tests\Release\tests.exe

if ( $? -eq $false ) {
  throw "tests failed"
}
