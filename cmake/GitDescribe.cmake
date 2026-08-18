# Grab the git describe output and store it in GIT_DESCRIBE
# Thanks to http://xit0.org/2013/04/cmake-use-git-branch-and-commit-details-in-project/
execute_process(
  COMMAND git describe --always --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DESCRIBE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "git describe returned \"${GIT_DESCRIBE}\"")

# git describe fails on GitHub Actions
if(NOT GIT_DESCRIBE)
	# See: https://docs.github.com/en/actions/reference/environment-variables#default-environment-variables
	if(NOT ("$ENV{GITHUB_REF_NAME}" STREQUAL ""))
		set(GIT_DESCRIBE "$ENV{GITHUB_REF_NAME}")
		
		message(STATUS "using version label \"${GIT_DESCRIBE}\" from GITHUB_REF_NAME")
	endif()
endif()
add_definitions(-DERICWTOOLS_VERSION="${PROJECT_VERSION}-${GIT_DESCRIBE}")

