set(IDF_DEPS
    driver
    ulp
)

set(CUSTOM_DEPS
    Config
)

idf_component_register(SRCS UlpAdc.cpp
                       INCLUDE_DIRS "include"
                       REQUIRES "${IDF_DEPS} ${CUSTOM_DEPS}")

ulp_embed_binary(ulp_adc_ops adc_ops.S UlpAdc.cpp)

