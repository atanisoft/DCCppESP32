# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

###############################################################################
# Validate that we are on a supported version of ESP-IDF (v4.3+)
###############################################################################
if (IDF_VERSION_MAJOR LESS 4)
    message(FATAL_ERROR "ESP32CommandStation requires IDF v4.0 (or newer)")
endif()

if (IDF_VERSION_MAJOR EQUAL 4 AND IDF_VERSION_MINOR LESS 3)
    message(FATAL_ERROR "ESP32 Command Station requires IDF v4.3 (or newer)")
endif()

###############################################################################
# Verify submodules were checked out correctly
###############################################################################

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/components/OpenMRNIDF/CMakeLists.txt)
    message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/components/OpenMRNIDF does not appear to have been checked out, please run 'git submodule update --init' from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/components/HttpServer/CMakeLists.txt)
    message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}/components/HttpServer does not appear to have been checked out, please run 'git submodule update --init' from ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

###############################################################################
# ESP32 Command Station only supports the ESP32 and not other variants at this
# time. This may change in the future.
###############################################################################
set(SUPPORTED_TARGETS esp32)

###############################################################################
# Disable ESP-IDF components that are not required for this project. This will
# reduce the compile time.
###############################################################################

set(EXCLUDE_COMPONENTS
    "asio" "bt" "cmock" "coap" "esp-tls" "esp_hid"
    "esp_http_client" "esp_http_server" "esp_https_ota" "esp_https_server"
    "esp_lcd" "esp_local_ctrl" "esp_serial_slave_link" "esp_websocket_client"
    "freemodbus" "idf_test" "mqtt" "nghttp" "perfmon" "protobuf-c" "protocomm"
    "tcp_transport" "tinyusb" "unity" "wifi_provisioning")

project(ESP32CommandStation)

###############################################################################
# Switch from GNU++11 to GNU++14
#
# NOTE: C++14 would be preferred but IDF has dependencies on GNU extensions.
###############################################################################

string(REPLACE "-std=gnu++11" "-std=gnu++14" CXX_OPTIONS "${CXX_COMPILE_OPTIONS}")
idf_build_set_property(CXX_COMPILE_OPTIONS "${CXX_OPTIONS}" REPLACE)

###############################################################################
# Enable usage of std::stoi/stol/etc
###############################################################################

idf_build_set_property(COMPILE_DEFINITIONS "-D_GLIBCXX_USE_C99" APPEND)

###############################################################################
# Silence warnings due to OpenMRN code.
###############################################################################

idf_build_set_property(COMPILE_DEFINITIONS "-Wno-ignored-qualifiers" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-Wno-implicit-fallthrough" APPEND)

###############################################################################
# Search for GZIP application
###############################################################################

FIND_PROGRAM(GZIP
  NAMES gzip
  PATHS /bin
        /usr/bin
        /usr/local/bin
)

if (NOT GZIP)
  message(FATAL_ERROR "Unable to find 'gzip' program")
endif()

###############################################################################
# Generate a compressed version of web content on-demand
###############################################################################

add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html.gz"
    COMMAND ${GZIP} -fk ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
    VERBATIM)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html.gz")

add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/web/cash.min.js.gz"
    COMMAND ${GZIP} -fk ${CMAKE_CURRENT_SOURCE_DIR}/web/cash.min.js
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/cash.min.js
    VERBATIM)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/web/cache.min.js.gz")

add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/web/spectre.min.css.gz"
    COMMAND ${GZIP} -fk ${CMAKE_CURRENT_SOURCE_DIR}/web/spectre.min.css
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/spectre.min.css
    VERBATIM)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/web/spectre.min.css.gz")

###############################################################################
# Add web content to the binary
###############################################################################

target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html.gz" BINARY)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${CMAKE_CURRENT_SOURCE_DIR}/web/cash.min.js.gz" BINARY)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${CMAKE_CURRENT_SOURCE_DIR}/web/spectre.min.css.gz" BINARY)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${CMAKE_CURRENT_SOURCE_DIR}/web/loco-32x32.png" BINARY)

###############################################################################
# Configuration validations
###############################################################################

if (NOT CONFIG_FREERTOS_HZ EQUAL 1000)
    message(FATAL_ERROR "FreeRTOS tick rate (hz) is required to be 1000.")
endif()

if (NOT CONFIG_PARTITION_TABLE_FILENAME STREQUAL "ESP32CS-partitions.csv")
    message(FATAL_ERROR "The custom partition table option is not enabled in menuconfig and is required for compilation.")
endif()

if (NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "ESP32CS-partitions.csv")
    message(FATAL_ERROR "The custom partition table option is not enabled in menuconfig and is required for compilation.")
endif()

if (NOT CONFIG_PARTITION_TABLE_CUSTOM)
    message(FATAL_ERROR "The custom partition table option is not enabled in menuconfig and is required for compilation.")
endif()

if (NOT CONFIG_LWIP_SO_RCVBUF)
    message(FATAL_ERROR "LwIP SO_RCVBUF is a required option in menuconfig.")
endif()

if (NOT CONFIG_ESP32_ULP_COPROC_ENABLED)
    message(FATAL_ERROR "ULP Co-Processor must be enabled")
endif()