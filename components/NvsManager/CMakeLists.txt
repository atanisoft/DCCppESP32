set(IDF_DEPS
    app_update
    nvs_flash
)

set(CUSTOM_DEPS
    Config
    OpenMRNIDF
    StatusLED
    Utils
)

idf_component_register(SRCS NvsManager.cpp
                       INCLUDE_DIRS "include"
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")