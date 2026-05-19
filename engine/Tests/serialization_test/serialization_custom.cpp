#include "serialization_custom.h"

#include "meta_serialization_custom/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationCustomTest() {
    using namespace SerializationTest;

    CustomTest custom_test;
    custom_test.m_a = 123;
    custom_test.m_b = 852765;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(custom_test, archive);

    custom_test.m_a = 0;
    custom_test.m_b = 0;

    CustomTest custom_test2;
    Engine::Serialization::deserialize(custom_test2, archive);

    assert(custom_test2.m_a == 123);
    assert(custom_test2.m_b == 852765);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationCustomTest();
    return 0;
}

#include "__generated__/serialization_custom.h.inc"
