# Root ESP-IDF entry point. Keep this file so VS Code can build from the repository root.
cmake_minimum_required(VERSION 3.16)

add_compile_options(-Wno-missing-field-initializers)

set(DEVICE_PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}/firmware/device")
set(EXTRA_COMPONENT_DIRS "${DEVICE_PROJECT_DIR}/main")
set(SDKCONFIG "${DEVICE_PROJECT_DIR}/sdkconfig")
set(SDKCONFIG_DEFAULTS
    "${DEVICE_PROJECT_DIR}/sdkconfig.defaults;${DEVICE_PROJECT_DIR}/sdkconfig.defaults.esp32s3")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.4.0")
project(immortal_pet)
