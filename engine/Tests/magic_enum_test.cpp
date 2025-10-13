
#include <Render/Enums/enum_def.h>
#include <cassert>
#include <iostream>

using namespace Engine::_enum;

#define MAGIC_ENUM_TEST_MACRO(enum_type, item) \
    assert(*Engine::Reflection::from_string<enum_type>(#item) == enum_type::item); \
    assert(Engine::Reflection::to_string(enum_type::item) == #item); \


int main() {
    COMPARATOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, Comparator)
    BLEND_FACTOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, BlendFactor)
    BLEND_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, BlendOperation)
    STENCIL_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, StencilOperation)
    IMAGE_FORMAT_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, ImageFormat)
    CULLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, CullingMode)
    FILLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, FillingMode)
    FRONT_FACE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, FrontFace)
    ADDRESS_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, AddressMode)
    FILTER_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO, FilterMode)
    
    // Test abnormal cases
    assert(!Engine::Reflection::from_string<AddressMode>("whatever this is"));
    assert(Engine::Reflection::to_string(static_cast<AddressMode>(-1)) == "");

    return 0;
}
