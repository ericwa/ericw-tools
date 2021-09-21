# for Groff (needed to generate manual)
# see: https://github.com/actions/virtual-environments/blob/main/images/win/scripts/Installers/Install-Msys2.ps1
$env:PATH += ";C:\msys64\mingw64\bin;C:\msys64\usr\bin"

# For sha256sum
$env:Path += ";C:\Program Files\Git\usr\bin"

# Fetch dependencies
Invoke-WebRequest 'https://github.com/embree/embree/releases/download/v3.12.1/embree-3.12.1.x64.vc14.windows.zip' -OutFile 'embree64.zip'
7z x embree64.zip -oc:\
Invoke-WebRequest 'https://github.com/oneapi-src/oneTBB/releases/download/v2020.2/tbb-2020.2-win.zip' -OutFile 'tbb.zip'
7z x tbb.zip -oc:\

mkdir cmakebuild
cd cmakebuild

cmake .. -T v142 -Dembree_DIR="C:\embree-3.12.1.x64.vc14.windows" -DTBB_DIR="C:\tbb\cmake" -DCMAKE_GENERATOR_PLATFORM=x64

$cmakePlatform = "x64"

msbuild /target:testlight /p:Configuration=Release /p:Platform=$cmakePlatform ericw-tools.sln

if ( $? -eq $false ) {
  throw "testlight failed to build"
}

msbuild /target:testqbsp /p:Configuration=Release /p:Platform=$cmakePlatform ericw-tools.sln

if ( $? -eq $false ) {
  throw "testqbsp failed to build"
}

msbuild /p:Configuration=Release /p:Platform=$cmakePlatform PACKAGE.vcxproj

if ( $? -eq $false ) {
  throw "package failed"
}

.\light\Release\testlight.exe

if ( $? -eq $false ) {
  throw "testlight failed"
}

.\qbsp\Release\testqbsp.exe

if ( $? -eq $false ) {
  throw "testqbsp failed"
}

$env:Path += ";$(pwd)\qbsp\Release;$(pwd)\vis\Release;$(pwd)\light\Release"

cd ..\testmaps

. "C:\Program Files\Git\usr\bin\bash.exe" .\automatated_tests.sh

if ( $LastExitCode -ne 0 ) {
  throw "automatated_tests.sh failed"
}
