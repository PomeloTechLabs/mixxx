vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO PortAudio/portaudio
    REF 147dd722548358763a8b649b3e4b41dfffbcfbb6
    SHA512 0f56e5f5b004f51915f29771b8fc1fe886f1fef5d65ab5ea1db43f43c49917476b9eec14b36aa54d3e9fb4d8bdf61e68c79624d00b7e548d4c493395a758233a
    PATCHES
        jack.diff
        fix-guid-linker-errors.patch
        use-vcpkg-asiosdk.patch
        # OHOS musl defines PTHREAD_CANCELED without pthread_cancel;
        # let PortAudio's own Android-style flag+join path handle it.
        ohos-pthread-cancel.patch
)

string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" PA_DLL_LINK_WITH_STATIC_RUNTIME)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" PA_BUILD_SHARED)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" PA_BUILD_STATIC)

# HarmonyOS/OHOS: add the OHAudio host API (see ohos/pa_ohos.c).
if(VCPKG_TARGET_TRIPLET MATCHES "ohos$")
    file(MAKE_DIRECTORY "${SOURCE_PATH}/src/hostapi/ohos")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/ohos/pa_ohos.c"
         DESTINATION "${SOURCE_PATH}/src/hostapi/ohos")

    # 1) public host API type id
    vcpkg_replace_string("${SOURCE_PATH}/include/portaudio.h"
        "    paAudioScienceHPI=14
} PaHostApiTypeId;"
        "    paAudioScienceHPI=14,
    paOHOS=15, /* HarmonyOS NEXT / OpenHarmony OHAudio */
} PaHostApiTypeId;")

    # 2) hostapi.h: PA_USE_OHOS switch and initializer declaration
    vcpkg_replace_string("${SOURCE_PATH}/src/common/pa_hostapi.h"
        "#ifndef PA_USE_SKELETON"
        "#ifndef PA_USE_OHOS
#define PA_USE_OHOS 0
#elif (PA_USE_OHOS != 0) && (PA_USE_OHOS != 1)
#undef PA_USE_OHOS
#define PA_USE_OHOS 1
#endif

PaError PaOhos_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index );
#if PA_USE_OHOS
/* nothing else to declare here */
#endif

#ifndef PA_USE_SKELETON")

    # 3) unix initializer table gets the OHAudio entry first
    vcpkg_replace_string("${SOURCE_PATH}/src/os/unix/pa_unix_hostapis.c"
        "PaError PaSkeleton_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index );"
        "PaError PaSkeleton_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index );
#if PA_USE_OHOS
PaError PaOhos_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex index );
#endif")

    vcpkg_replace_string("${SOURCE_PATH}/src/os/unix/pa_unix_hostapis.c"
        "PaUtilHostApiInitializer *paHostApiInitializers[] =
    {"
        "PaUtilHostApiInitializer *paHostApiInitializers[] =
    {
#if PA_USE_OHOS
        PaOhos_Initialize,
#endif")

    # 4) CMake: build the source, define the switch, link OHAudio
    vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt"
        "SET(PA_SOURCES ${PA_COMMON_SOURCES} ${PA_SKELETON_SOURCES})"
        "SET(PA_SOURCES ${PA_COMMON_SOURCES} ${PA_SKELETON_SOURCES})
IF(CMAKE_SYSTEM_NAME STREQUAL \"OHOS\")
  SET(PA_OHOS_SOURCES src/hostapi/ohos/pa_ohos.c)
  SOURCE_GROUP(\"hostapi\\\\ohos\" FILES ${PA_OHOS_SOURCES})
  SET(PA_SOURCES ${PA_SOURCES} ${PA_OHOS_SOURCES})
  ADD_DEFINITIONS(-DPA_USE_OHOS=1)
ENDIF()")

    file(APPEND "${SOURCE_PATH}/CMakeLists.txt" "
IF(CMAKE_SYSTEM_NAME STREQUAL \"OHOS\")
  TARGET_LINK_LIBRARIES(portaudio ohaudio hilog_ndk.z)
  TARGET_LINK_LIBRARIES(portaudio_static ohaudio hilog_ndk.z)
ENDIF()
")
endif()

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        asio PA_USE_ASIO
)

vcpkg_list(SET options)
if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_list(APPEND options
        -DPA_DLL_LINK_WITH_STATIC_RUNTIME=${PA_DLL_LINK_WITH_STATIC_RUNTIME}
        -DPA_LIBNAME_ADD_SUFFIX=OFF
    )
elseif(VCPKG_TARGET_IS_IOS OR VCPKG_TARGET_IS_OSX)
    vcpkg_list(APPEND options
        # avoid absolute paths
        -DCOREAUDIO_LIBRARY:STRING=-Wl,-framework,CoreAudio
        -DAUDIOTOOLBOX_LIBRARY:STRING=-Wl,-framework,AudioToolbox
        -DAUDIOUNIT_LIBRARY:STRING=-Wl,-framework,AudioUnit
        -DCOREFOUNDATION_LIBRARY:STRING=-Wl,-framework,CoreFoundation
        -DCORESERVICES_LIBRARY:STRING=-Wl,-framework,CoreServices
    )
else()
    vcpkg_list(APPEND options
        -DPA_USE_JACK=ON
        -DCMAKE_REQUIRE_FIND_PACKAGE_Jack=ON
        -DPA_USE_ALSA=OFF
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${options}
        -DPA_BUILD_SHARED=${PA_BUILD_SHARED}
        -DPA_BUILD_STATIC=${PA_BUILD_STATIC}
        -DPA_USE_ASIO=${PA_USE_ASIO}
    OPTIONS_DEBUG
        -DPA_ENABLE_DEBUG_OUTPUT:BOOL=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/${PORT})
vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
    "${CURRENT_PACKAGES_DIR}/share/doc"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
