set(CUSTOM_DEPS
    Config
    OpenMRNIDF
    NeoPixelBus
)

idf_component_register(SRCS StatusLED.cpp
                       INCLUDE_DIRS "include"
                       REQUIRES "${CUSTOM_DEPS}")

set_source_files_properties(StatusLED.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)