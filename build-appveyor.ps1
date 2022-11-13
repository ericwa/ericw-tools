# Download embree and tbb
# Seems like TBB dropped Windows 8.1 support in version 2020 so we'll use older versions
Invoke-WebRequest 'https://github.com/embree/embree/releases/download/v3.9.0/embree-3.9.0.x64.vc14.windows.zip' -OutFile 'embree64.zip'
7z x embree64.zip -oc:\
Invoke-WebRequest 'https://github.com/oneapi-src/oneTBB/releases/download/2019_U9/tbb2019_20191006oss_win.zip' -OutFile 'tbb.zip'
7z x tbb.zip -oc:\

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

cmake .. -T v143 -Dembree_DIR="C:\embree-3.9.0.x64.vc14.windows" -DTBB_DIR="C:\tbb2019_20191006oss\cmake" -DCMAKE_GENERATOR_PLATFORM=x64 -DLEGACY_EMBREE=YES

$cmakePlatform = "x64"

msbuild /p:Configuration=Release /p:Platform=$cmakePlatform /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll" PACKAGE.vcxproj

if ( $? -eq $false ) {
  throw "package failed"
}

.\tests\Release\tests.exe --no-skip

if ( $? -eq $false ) {
  throw "tests failed"
}
