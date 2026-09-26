# Injected into Qt cross builds via CMAKE_PROJECT_TOP_LEVEL_INCLUDES.
# Runs after the toolchain loads, before qtbase's project() code.
#
# Qt's ohos QPA links fontconfig from the vcpkg arm64-ohos prefix, and
# vcpkg's freetype config file references BZip2::BZip2 / ZLIB::ZLIB which
# Qt never creates on its own. We cannot run the stock Find modules here:
# at project-include time no language is enabled, so FindBZip2's symbol
# check fails. Build the imported targets by hand instead.

set(_MIXXX_VCPKG_PREFIX "/data/vcpkg/vcpkg/installed/arm64-ohos")

if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB UNKNOWN IMPORTED GLOBAL)
  set_target_properties(ZLIB::ZLIB PROPERTIES
    IMPORTED_GLOBAL TRUE
    IMPORTED_LOCATION "${_MIXXX_VCPKG_PREFIX}/lib/libz.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_MIXXX_VCPKG_PREFIX}/include"
  )
endif()

if(NOT TARGET BZip2::BZip2)
  add_library(BZip2::BZip2 UNKNOWN IMPORTED GLOBAL)
  set_target_properties(BZip2::BZip2 PROPERTIES
    IMPORTED_GLOBAL TRUE
    IMPORTED_LOCATION "${_MIXXX_VCPKG_PREFIX}/lib/libbz2.a"
    INTERFACE_INCLUDE_DIRECTORIES "${_MIXXX_VCPKG_PREFIX}/include"
  )
endif()

# expat is a static dependency of fontconfig; Qt's shared libs get it via
# LDFLAGS (see build-qt-ohos.sh).
