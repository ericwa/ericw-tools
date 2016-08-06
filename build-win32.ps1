#build embree
Invoke-WebRequest 'https://github.com/embree/embree/archive/v2.10.0.zip' -OutFile 'embree.zip'
7z x embree.zip
cd embree-2.10.0
mkdir build
cd build
cmake .. -T v140_xp -DENABLE_ISPC_SUPPORT=NO -DENABLE_TUTORIALS=NO -DRTCORE_TASKING_SYSTEM=INTERNAL -DCMAKE_INSTALL_PREFIX="$(get-location)"
msbuild INSTALL.vcxproj /p:Configuration=Release
$EmbreeInstallDir = [io.path]::combine($(get-location).Path, "lib\cmake\embree-2.10.0")
cd ..
cd ..

# build tyrutils
mkdir cmakebuild
cd cmakebuild
cmake .. -T v140_xp -Dembree_DIR="$EmbreeInstallDir"
msbuild PACKAGE.vcxproj /p:Configuration=Release /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll"
