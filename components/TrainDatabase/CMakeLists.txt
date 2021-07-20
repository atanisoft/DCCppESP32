set(CUSTOM_DEPS
    OpenMRNIDF
    TrainSearchProtocol
    Utils
)

set(IDF_DEPS
    json
)

idf_component_register(SRCS Esp32TrainDatabase.cpp Esp32TrainDbEntry.cpp
                       INCLUDE_DIRS include
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")

set_source_files_properties(Esp32TrainDatabase.cpp PROPERTIES COMPILE_FLAGS -Wno-ignored-qualifiers)
