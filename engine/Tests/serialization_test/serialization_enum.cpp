#include "serialization_enum.h"

#include "meta_serialization_enum/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationEnumTest() {
    using namespace SerializationTest;

    EnumTest enum_test;
    enum_test.m_color = EnumTest::Color::Blue;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(enum_test, archive);

    enum_test.m_color = EnumTest::Color::Red;

    EnumTest enum_test2;
    Engine::Serialization::deserialize(enum_test2, archive);

    assert(enum_test2.m_color == EnumTest::Color::Blue);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationEnumTest();
    return 0;
}

#include "__generated__/serialization_enum.h.inc"
