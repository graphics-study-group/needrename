#include "serialization_nested_struct.h"

#include "meta_serialization_nested_struct/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationNestedStructTest() {
    using namespace SerializationTest;

    StructStructTest struct_struct_test;
    struct_struct_test.m_inner_struct.m_inner_float = 3.1415926f;
    struct_struct_test.m_inner_struct.m_inner_int = 100;
    struct_struct_test.m_inner_class.m_inner_int = 200;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(struct_struct_test, archive);

    struct_struct_test.m_inner_struct.m_inner_float = 0.0f;
    struct_struct_test.m_inner_struct.m_inner_int = 0;
    struct_struct_test.m_inner_class.m_inner_int = 0;

    StructStructTest struct_struct_test2;
    Engine::Serialization::deserialize(struct_struct_test2, archive);

    assert(struct_struct_test2.m_inner_struct.m_inner_float == 3.1415926f);
    assert(struct_struct_test2.m_inner_struct.m_inner_int == 100);
    assert(struct_struct_test2.m_inner_class.m_inner_int == 200);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationNestedStructTest();
    return 0;
}

#include "__generated__/serialization_nested_struct.h.inc"
