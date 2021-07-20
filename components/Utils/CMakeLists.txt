
set(IDF_DEPS
    fatfs
    spi_flash
    spiffs
    vfs
)

set(CUSTOM_DEPS
    Config
    StatusDisplay
    StatusLED
)

idf_component_register(SRCS FileSystem.cpp
                       INCLUDE_DIRS include
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")