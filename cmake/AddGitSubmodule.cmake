cmake_minimum_required(VERSION 3.19)

function(add_git_submodule dir)
# add a Git submodule directory to CMake, assuming the
# Git submodule directory is a CMake project.
#
# Usage: in CMakeLists.txt
# 
# include(AddGitSubmodule.cmake)
# add_git_submodule(mysubmod_dir)

find_package(Git REQUIRED)

message(STATUS "Adding sub_directory ${dir}")
execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- ${dir}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND /bin/sh ${CMAKE_CURRENT_SOURCE_DIR}/update_deps_commit.sh)
add_subdirectory(${dir})

endfunction(add_git_submodule)
