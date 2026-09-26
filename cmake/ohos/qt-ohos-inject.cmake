# Injected into Qt cross builds via CMAKE_PROJECT_TOP_LEVEL_INCLUDES.
# Runs after the toolchain loads, before qtbase's project() code.
#
# Qt's ohos QPA links fontconfig from the vcpkg arm64-ohos prefix. Qt's own
# configure never creates the BZip2::BZip2 / ZLIB::ZLIB imported targets that
# vcpkg's freetype config file references, so locate the libs here with
# explicit HINTS + NO_DEFAULT_PATH (CMake's re-rooted search never produces
# <vcpkg-prefix>/include as a candidate in this setup) and let the standard
# Find modules build the imported targets from the cached variables.

set(_MIXXX_VCPKG_PREFIX "/data/vcpkg/vcpkg/installed/arm64-ohos")

find_path(ZLIB_INCLUDE_DIR zlib.h HINTS "${_MIXXX_VCPKG_PREFIX}/include" NO_DEFAULT_PATH)
find_library(ZLIB_LIBRARY NAMES z libz HINTS "${_MIXXX_VCPKG_PREFIX}/lib" NO_DEFAULT_PATH)
find_package(ZLIB REQUIRED)

find_path(BZIP2_INCLUDE_DIR bzlib.h HINTS "${_MIXXX_VCPKG_PREFIX}/include" NO_DEFAULT_PATH)
find_library(BZIP2_LIBRARY NAMES bz2 HINTS "${_MIXXX_VCPKG_PREFIX}/lib" NO_DEFAULT_PATH)
find_package(BZip2 REQUIRED)

find_path(PNG_PNG_INCLUDE_DIR png.h HINTS "${_MIXXX_VCPKG_PREFIX}/include" NO_DEFAULT_PATH)
find_library(PNG_LIBRARY NAMES png16 libpng16 HINTS "${_MIXXX_VCPKG_PREFIX}/lib" NO_DEFAULT_PATH)
find_package(PNG QUIET)
