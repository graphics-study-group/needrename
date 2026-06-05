#include "serialization_inheritance.h"

#include "meta_serialization_inheritance/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationInheritanceTest() {
    using namespace SerializationTest;

    InheritTest inherit_test;
    for (int i = 0; i < 3; i++) {
        inherit_test.data[i] = 182.376f * i;
    }
    inherit_test.m_inherit = 1000;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(inherit_test, archive);

    inherit_test.data[0] = 0;
    inherit_test.m_inherit = 0;

    InheritTest inherit_test2;
    Engine::Serialization::deserialize(inherit_test2, archive);

    for (int i = 0; i < 3; i++) assert(inherit_test2.data[i] == 182.376f * i);
    assert(inherit_test2.m_inherit == 1000);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationInheritanceTest();
    return 0;
}

#include "__generated__/serialization_inheritance.h.inc"
