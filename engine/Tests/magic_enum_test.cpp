
#include <Render/Enums/enum_def.h>
#include <cassert>
#include <iostream>

using namespace Engine::_enum;

#define MAGIC_ENUM_TEST_MACRO(enum_type, item) \
    assert(*to_##enum_type(#item) == enum_type::item); \
    assert(to_string(enum_type::item) == #item); \


int main() {
    COMPARATOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    COLOR_BLEND_FACTOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    COLOR_BLEND_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    STENCIL_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    IMAGE_FORMAT_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    RASTERIZER_CULLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    RASTERIZER_FILLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    RASTERIZER_FRONT_FACE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    SAMPLER_ADDRESS_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    SAMPLER_FILTER_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)

    // Test abnormal cases
    assert(!to_SamplerAddressMode("whatever this is"));
    assert(to_string(static_cast<SamplerAddressMode>(-1)) == "");

    return 0;
}
