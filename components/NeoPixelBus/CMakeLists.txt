set(IDF_DEPS "driver")

idf_component_register(SRC_DIRS "src/internal"
                       INCLUDE_DIRS "src"
                       REQUIRES "${IDF_DEPS}")
