set(IDF_DEPS
    driver
)

set(CUSTOM_DEPS
    Config
    NvsManager
    OpenMRNIDF
    TrainSearchProtocol
)

idf_component_register(SRCS StatusDisplay.cpp StatusDisplayLCD.cpp StatusDisplayOLED.cpp
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")

set_source_files_properties(StatusLED.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)