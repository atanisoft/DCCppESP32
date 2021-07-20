set(IDF_DEPS
    driver
    esp_adc_cal
    esp_timer
    ulp
    vfs
)

set(CUSTOM_DEPS
    Config
    AccessoryDecoderDB
    OpenMRNIDF
    StatusDisplay
    TrainSearchProtocol
    ULPADC
    Utils
)

idf_component_register(SRCS DccConstants.cpp DCCSignalVFS.cpp PrioritizedUpdateLoop.cpp
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")

set_source_files_properties(DCCSignalVFS.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)
set_source_files_properties(DuplexedTrackIf.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)
set_source_files_properties(PrioritizedUpdateLoop.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)
