set(_ohos_version_file "${CMAKE_SOURCE_DIR}/packaging/ohos/version.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ohos_version_file}")
file(READ "${_ohos_version_file}" _ohos_version_json)
string(JSON MIXXX_OHOS_REVISION GET "${_ohos_version_json}" revision)
string(JSON _ohos_revision_type TYPE "${_ohos_version_json}" revision)
if(NOT _ohos_revision_type STREQUAL "NUMBER" OR NOT MIXXX_OHOS_REVISION MATCHES "^(0|[1-9][0-9]*)$" OR MIXXX_OHOS_REVISION GREATER 999)
  message(FATAL_ERROR "OHOS revision must be an integer from 0 to 999")
endif()
set(MIXXX_OHOS_VERSION "${CMAKE_PROJECT_VERSION}.${MIXXX_OHOS_REVISION}")
if(NOT MIXXX_VERSION_PRERELEASE STREQUAL "")
  string(APPEND MIXXX_OHOS_VERSION "-${MIXXX_VERSION_PRERELEASE}")
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/src/platform/ohos/version.h.in"
  "${CMAKE_BINARY_DIR}/src/platform/ohos/version.h"
  @ONLY
)
