
#include <Render/Enums/enum_def.h>
#include <cassert>
#include <iostream>

using namespace Engine::_enum;

#define MAGIC_ENUM_TEST_MACRO(enum_type, item) \
    assert(to_##enum_type(#item) == enum_type::item); \
    assert(to_string(enum_type::item) == #item); \


int main() {
    COMPARATOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    BLEND_FACTOR_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    BLEND_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    STENCIL_OPERATION_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    IMAGE_FORMAT_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    CULLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    FILLING_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    FRONT_FACE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    ADDRESS_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)
    FILTER_MODE_ENUM_DEF(MAGIC_ENUM_TEST_MACRO)

    // Test abnormal cases
    try {
        to_AddressMode("whatever this is");
    } catch (std::exception & e) {
        std::cout << e.what() << std::endl ;
    }

    assert(to_string(static_cast<AddressMode>(-1)) == "");

    return 0;
}
