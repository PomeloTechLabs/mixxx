# Mixxx OHOS HAP shell: builds the Qt boot library.
# Qt for OHOS install tree is provided by the caller via QT_OHOS_PREFIX.
cmake_minimum_required(VERSION 3.10)
project(qtmixxxboot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

if(NOT DEFINED QT_OHOS_PREFIX)
  set(QT_OHOS_PREFIX "/data/out/qt-ohos")
endif()
if(NOT DEFINED QT_HOST_PREFIX)
  set(QT_HOST_PREFIX "/data/out/qt-host")
endif()

# re-rooted search requires the prefix as a FIND_ROOT_PATH entry
# (usr -> . symlink exists in the Qt install tree)
list(APPEND CMAKE_FIND_ROOT_PATH "${QT_OHOS_PREFIX}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_add_executable(qtmixxxboot SHARED qtmixxxboot.cpp)

set_target_properties(qtmixxxboot PROPERTIES OUTPUT_NAME "qtmixxxboot")

qt_add_qml_module(qtmixxxboot
    URI mixxx.ohos.boot
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(qtmixxxboot PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
