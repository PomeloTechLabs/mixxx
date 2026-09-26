# Injected into Qt cross builds via CMAKE_PROJECT_TOP_LEVEL_INCLUDES.
# Runs after the toolchain loads, before qtbase's project() code.
#
# Qt's ohos QPA links fontconfig, found from the vcpkg arm64-ohos prefix
# (see build-qt-ohos.sh). vcpkg's freetype config file references
# BZip2::BZip2 / ZLIB::ZLIB / PNG::PNG imported targets that Qt never
# looks up on its own, so create them here, from the same prefix.
# fontconfig's static archive also pulls in expat at runtime; that is
# handled via CMAKE_SHARED_LINKER_FLAGS in the build script.

find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(PNG QUIET)
