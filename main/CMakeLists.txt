###############################################################################
# ESP32 COMMAND STATION
#
# COPYRIGHT (c) 2017-2021 Mike Dunston
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see http://www.gnu.org/licenses
###############################################################################

set(IDF_DEPS
    app_update
    bootloader_support
    driver
    efuse
    esp_adc_cal
    esp_common
    espcoredump
    json
    nvs_flash
    soc
)

set(CUSTOM_DEPS
    AccessoryDecoderDB
    Config
    DCC
    HttpServer
    NeoPixelBus
    NvsManager
    OpenMRNIDF
    StatusDisplay
    StatusLED
    TrainDatabase
    TrainSearchProtocol
    ULPADC
    Utils
)

idf_component_register(SRCS ESP32CommandStation.cpp Esp32CoreDumpUtil.cpp WebServer.cpp
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")

# export the project version as a define for the SNIP data, note it must be
# truncated to 21 characters max.
idf_build_get_property(project_ver PROJECT_VER)
string(SUBSTRING "${project_ver}" 0 20 SNIP_SW_VERSION)
idf_build_get_property(project_name PROJECT_NAME)
string(SUBSTRING "${project_name}" 0 40 SNIP_PROJECT_NAME)

set(SNIP_HW_VERSION "1.0.0")
set(SNIP_PROJECT_PAGE "atanisoft.github.io/ESP32CommandStation")
set(CDI_VERSION "0x0200")
message(STATUS "SNIP: ${SNIP_PROJECT_NAME}/${SNIP_PROJECT_PAGE}/${SNIP_SW_VERSION}/${SNIP_HW_VERSION}")

set_source_files_properties(ESP32CommandStation.cpp PROPERTIES COMPILE_FLAGS "-Wno-implicit-fallthrough -Wno-ignored-qualifiers")
set_source_files_properties(ESP32CommandStation.cpp PROPERTIES COMPILE_DEFINITIONS "SNIP_PROJECT_PAGE=\"${SNIP_PROJECT_PAGE}\"; SNIP_HW_VERSION=\"${SNIP_HW_VERSION}\"; SNIP_SW_VERSION=\"${SNIP_SW_VERSION}\"; SNIP_PROJECT_NAME=\"${SNIP_PROJECT_NAME}\"; CDI_VERSION=${CDI_VERSION}")
set_source_files_properties(nvs_config.cpp PROPERTIES COMPILE_FLAGS "-Wno-ignored-qualifiers")