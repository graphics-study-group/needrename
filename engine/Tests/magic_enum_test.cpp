
#include <Render/Enums/enum_def.h>
#include <cassert>
#include <iostream>

using namespace Engine::_enum;

#define MAGIC_ENUM_TEST_MACRO(enum_type, item) \
    assert(*Engine::Reflection::from_string<enum_type>(#item) == enum_type::item); \
    assert(Engine::Reflection::to_string(enum_type::item) == #item); \


int main() {
    COMPARATOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, Comparator)
    COLOR_BLEND_FACTOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, ColorBlendFactor)
    COLOR_BLEND_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, ColorBlendOperation)
    STENCIL_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, StencilOperation)
    IMAGE_FORMAT_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, ImageFormat)
    RASTERIZER_CULLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, RasterizerCullingMode)
    RASTERIZER_FILLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, RasterizerFillingMode)
    RASTERIZER_FRONT_FACE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, RasterizerFrontFace)
    SAMPLER_ADDRESS_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, SamplerAddressMode)
    SAMPLER_FILTER_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, SamplerFilterMode)
    
    // Test abnormal cases
    assert(!Engine::Reflection::from_string<SamplerAddressMode>("whatever this is"));
    assert(Engine::Reflection::to_string(static_cast<SamplerAddressMode>(-1)) == "");

    return 0;
}
