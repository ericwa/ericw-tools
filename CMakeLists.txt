cmake_minimum_required (VERSION 3.14)
cmake_policy(SET CMP0028 NEW)

project (ericw-tools
	VERSION 2.0.0
	LANGUAGES CXX C
	HOMEPAGE_URL https://github.com/ericwa/ericw-tools
	DESCRIPTION "Quake/Hexen 2 Map compiling tools - branch of http://disenchant.net/utils"
)

# set our C/C++ dialects
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)

option(ENABLE_PREFIX_INSTALL "Enable Prefix install" OFF)

if(NOT CMAKE_GENERATOR MATCHES "^VisualStudio")
  include(GNUInstallDirs)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/3rdparty/Catch2/extras")
include(GitDescribe)

include_directories($<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

string(TOUPPER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROCESSOR_UPPER)
message(STATUS "${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR} ${CMAKE_C_COMPILER_ID}/${CMAKE_C_COMPILER_FRONTEND_VARIANT} => ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR} ${CMAKE_C_COMPILER_ID}/${CMAKE_C_COMPILER_FRONTEND_VARIANT}")

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "GNU|AppleClang")
	if (NOT CMAKE_CROSSCOMPILING)
		add_compile_options(-march=native)
		if (${SYSTEM_PROCESSOR_UPPER} MATCHES "X64|X86_64|AMD64")
			add_compile_options(-mfpmath=sse)
		endif()
		if (${SYSTEM_PROCESSOR_UPPER} MATCHES "PPC|POWERPC|PPC64|PPC64LE")
			include(CheckCSourceCompiles)

			set(CMAKE_REQUIRED_FLAGS "-maltivec")
			check_c_source_compiles("#include <altivec.h> int main() { vector int v = (vector int){0}; return 0; }" HAVE_ALTIVEC)

			if(HAVE_ALTIVEC)
				add_compile_options(-maltivec)
			endif()
		endif()
	endif()
	add_compile_options(-ftree-vectorize)
	add_compile_options(-fopenmp)
	if (NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "AppleClang")
		add_compile_options(-fopenmp-simd)
		add_compile_options(-fexpensive-optimizations)
	endif()
	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		add_compile_options(-Wno-gnu-alignof-expression)
	endif()
	add_compile_options(-ffunction-sections)
	add_compile_options(-fdata-sections)
	add_compile_options(-fno-asynchronous-unwind-tables)
	if (CMAKE_BUILD_TYPE MATCHES ReleaseStrip)
		add_link_options(-Wl,--gc-sections)
		add_link_options(-Wl,-s)
	endif()
	if (CMAKE_BUILD_TYPE MATCHES Debug)
		        add_compile_options(-g)
	else()
		        add_compile_options(-O3)
	endif()
	link_libraries(m)
elseif (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "MSVC")
	message(STATUS "working around exceptions not being enabled by default on clang-cl")
	add_compile_options(/EHsc)
        add_compile_options(/openmp:experimental)
        add_compile_options(/Zc:__cplusplus)
        add_compile_options(/std:c++latest)
        add_compile_options(/Zc:preprocessor)
        add_compile_options(/Z7)
        add_compile_options(/Gy)                                        # Function-level linking (puts each function in its own section)
        add_compile_options(/Gw)                                        # Optimize global data (puts each variable in its own section)
	# request 8MB stack for all .exe's
	set(STACKSIZE 16388608)
	set(CMAKE_EXE_LINKER_FLAGS "/STACK:${STACKSIZE}")

	add_definitions("/wd4244") # disable "conversion from .. to .., possible loss of data" warning
	add_definitions("/wd4018") # disable "signed/unsigned mismatch" warning
	add_definitions("/wd4200") # disable "nonstandard extension used: zero-sized array in struct/union" warning
	add_definitions("/wd4264") # disable "conversion from 'size_t' to 'int', possible loss of data" warning
	add_definitions("/wd4267") # disable "conversion from 'size_t' to 'int', possible loss of data" warning
	add_definitions("/wd4305") # disable "truncation from 'double' to 'float'" warning
	add_definitions("/wd4250") # disable "inherits via dominance' warning (used in trace_embree.cc)
        if (NOT CMAKE_CROSSCOMPILING)
                add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WIN32)
                if (CMAKE_VS_PLATFORM_NAME MATCHES "x64|Win32")
                        #add_compile_options(/Gv)
                endif()
        endif()
        if (CMAKE_BUILD_TYPE MATCHES ReleaseStrip)
                add_link_options(/DEBUG:NONE)
                add_link_options(/OPT:REF)                              # Eliminate unreferenced functions and data
                add_link_options(/VERBOSE:REF)                  # Print out everything the linker throws away
        endif()
        if (CMAKE_BUILD_TYPE MATCHES Debug)
                add_compile_options(/Zi)
                add_link_options(/DEBUG:FULL)
        else()
                add_compile_options(/O2)
        endif()
        add_link_options(/INCREMENTAL:NO)
else()
	message(FATAL_ERROR "Unsupported compiler frontend variant!")
endif()

find_package (Threads)

if (CMAKE_USE_PTHREADS_INIT)
	add_definitions(-DUSE_PTHREADS)
elseif (CMAKE_USE_WIN32_THREADS_INIT)
	add_definitions(-DUSE_WIN32THREADS)
endif ()

if (UNIX)
	add_definitions(-DLINUX)
endif (UNIX)

if (UNIX AND NOT APPLE)
	set(CMAKE_INSTALL_RPATH "$ORIGIN")
	set(CMAKE_BUILD_WITH_INSTALL_RPATH YES)
endif ()

# MINGW stuff
if(MINGW)
	find_file(LIB_GCC_S_SEH_1_DLL NAMES "libgcc_s_seh-1.dll")
	find_file(LIB_STDCPP_6_DLL NAMES "libstdc++-6.dll")
	find_file(LIB_WINPTHREAD_1_DLL NAMES "libwinpthread-1.dll")

	if(LIB_GCC_S_SEH_1_DLL)
		install(FILES ${LIB_GCC_S_SEH_1_DLL} DESTINATION .)
	endif()
	if(LIB_STDCPP_6_DLL)
		install(FILES ${LIB_STDCPP_6_DLL} DESTINATION .)
	endif()
	if(LIB_WINPTHREAD_1_DLL)
		install(FILES ${LIB_WINPTHREAD_1_DLL} DESTINATION .)
	endif()
endif()

function(copy_mingw_dlls TARGETNAME)
	if (LIB_GCC_S_SEH_1_DLL)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_GCC_S_SEH_1_DLL}" "$<TARGET_FILE_DIR:${TARGETNAME}>"
		)
	endif()
	if (LIB_STDCPP_6_DLL)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_STDCPP_6_DLL}" "$<TARGET_FILE_DIR:${TARGETNAME}>"
		)
	endif()
	if (LIB_WINPTHREAD_1_DLL)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB_WINPTHREAD_1_DLL}" "$<TARGET_FILE_DIR:${TARGETNAME}>"
		)
	endif()
endfunction()

# so the executable will search for dylib's in the same directory as the executable
function(add_loader_path_to_rpath TARGETNAME)
	if(APPLE)
		add_custom_command(TARGET ${TARGETNAME} POST_BUILD
			COMMAND bash ARGS -c \"install_name_tool -add_rpath @loader_path $<TARGET_FILE:${TARGETNAME}> || true\")
	endif()
endfunction()

if (WIN32)
	set("NO_ITERATOR_DEBUG" FALSE CACHE BOOL "Whether to use MSVC iterator debugging or not")

	if (NO_ITERATOR_DEBUG)
		add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)
		add_definitions(-D_CONTAINER_DEBUG_LEVEL=0)
	endif (NO_ITERATOR_DEBUG)

	# TODO: remove these
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif (WIN32)

# Pass -DERICWTOOLS_ASAN=YES to enable for all targets
if (ERICWTOOLS_ASAN)
	message(STATUS "Enabling ASan on all targets")
	add_compile_options(-fsanitize=address)
	add_link_options(-fsanitize=address)
endif()

if (ERICWTOOLS_TIMETRACE)
	message(STATUS "Enabling -ftime-trace")
	add_compile_options(-ftime-trace)
endif()


# 10.9: minimum version that supports unordered_map
# 10.14: required by tbb 2021.3.0 (due to use of alignas)
# 10.15: required by std::filesytstem
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15)

find_package(TBB REQUIRED)
find_package(embree 4 REQUIRED)

set(TEST_QUAKE_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Quake maps to this directory (useful for testing in game)")
set(TEST_QUAKE2_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Quake 2 maps to this directory (useful for testing in game)")
set(TEST_HEXEN2_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Hexen 2 maps to this directory (useful for testing in game)")
set(TEST_HALFLIFE_MAP_EXPORT_DIR "" CACHE PATH "When running unit tests, export Half-Life maps to this directory (useful for testing in game)")

add_subdirectory(3rdparty)
add_subdirectory(common)
add_subdirectory(bspinfo)
add_subdirectory(bsputil)
add_subdirectory(light)
add_subdirectory(qbsp)
add_subdirectory(vis)
add_subdirectory(maputil)

option(DISABLE_TESTS "Disables Tests" OFF)
option(DISABLE_DOCS "Disables Docs" OFF)
option(ENABLE_LIGHTPREVIEW "Enable light preview tool" ON)

if (ENABLE_LIGHTPREVIEW)
	add_subdirectory(lightpreview)
endif ()

if(NOT DISABLE_TESTS)
	# just creates testmaps.hh with absolute path to the testmaps source directory.
	# include it as #include <testmaps.hh>
	configure_file(testmaps.hh.in testmaps.hh @ONLY)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})
	add_subdirectory(tests)

	#CTest
	enable_testing()
endif ()

if(NOT DISABLE_DOCS)
	add_subdirectory(docs)
endif ()

if(ENABLE_PREFIX_INSTALL)
	install(FILES README.md DESTINATION ${CMAKE_INSTALL_FULL_DOCDIR})
else()
	install(FILES README.md DESTINATION .)
endif ()

#CPack configuration

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME ericw-tools)
set(CPACK_PACKAGE_VERSION ${GIT_DESCRIBE})
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
include(CPack)
