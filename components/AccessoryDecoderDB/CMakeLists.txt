set(IDF_DEPS
    json
)

set(CUSTOM_DEPS
    Config
    HttpServer
    OpenMRNIDF
    Utils
)

idf_component_register(SRCS AccessoryDecoderConstants.cpp AccessoryDecoderDB.cpp DccAccessoryDecoder.cpp OpenLCBAccessoryDecoder.cpp
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "private_include"
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")

set_source_files_properties(AccessoryDecoderDB.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)