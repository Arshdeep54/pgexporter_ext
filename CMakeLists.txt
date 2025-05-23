cmake_minimum_required(VERSION 3.14)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "3")
set(VERSION_PATCH "0")
set(VERSION_STRING ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

#
# Avoid source tree pollution
#
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

If(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not permitted. Make a separate folder for building:\nmkdir build; cd build; cmake ..\nBefore that, remove the files already created:\nrm -rf CMakeCache.txt CMakeFiles")
endif(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)

project(pgexporter_ext VERSION ${VERSION_STRING} LANGUAGES  C)

set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_SOURCE_IGNORE_FILES
  "/build/;/.git/;/.github/;/*.patch;/.bundle/;/_site/;/vendor/;~$;${CPACK_SOURCE_IGNORE_FILES}")
include(CPack)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "pgexporter_ext ${VERSION_STRING}")

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(FindPackageHandleStandardArgs)
include(GNUInstallDirs)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release" FORCE)
endif ()

message(STATUS "Build type is ${CMAKE_BUILD_TYPE}")

set(SUPPORTED_COMPILERS "GNU" "Clang" "AppleClang")

# Check for a supported compiler
if (NOT CMAKE_C_COMPILER_ID IN_LIST SUPPORTED_COMPILERS)
   message(FATAL_ERROR "Unsupported compiler ${CMAKE_C_COMPILER_ID}. Supported compilers are: ${SUPPORTED_COMPILERS}")
endif ()

CHECK_C_COMPILER_FLAG("-std=c17" COMPILER_SUPPORTS_C17)
if(NOT COMPILER_SUPPORTS_C17)
  message(FATAL_ERROR "The compiler ${CMAKE_C_COMPILER} has no C17 support. Please use a different C compiler.")
endif()

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/src/")

find_package(ZLIB)
if (ZLIB_FOUND)
  message(STATUS "zlib found")
else ()
  message(FATAL_ERROR "zlib needed")
endif()

find_package(BZip2)
if (BZIP2_FOUND)
  message(STATUS "bzip2 found")
else ()
  message(FATAL_ERROR "bzip2 needed")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LZ4 REQUIRED liblz4)
if (LZ4_FOUND)
    message(STATUS "Found LZ4: ${LZ4_LIBRARIES}")
else()
    message(FATAL_ERROR "LZ4 needed")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)
if (ZSTD_FOUND)
    message(STATUS "Found ZSTD: ${ZSTD_LIBRARIES}")
else()
    message(FATAL_ERROR "ZSTD needed")
endif()

add_subdirectory(src)
add_subdirectory(sql)
