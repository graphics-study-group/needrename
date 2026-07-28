#include "serialization_enum.h"

#include "meta_annorefl_serialization_enum/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <AnnoRefl/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationEnumTest() {
    using namespace SerializationTest;

    EnumTest enum_test;
    enum_test.m_color = EnumTest::Color::Blue;

    AnnoRefl::Archive archive;
    AnnoRefl::serialize(enum_test, archive);

    enum_test.m_color = EnumTest::Color::Red;

    EnumTest enum_test2;
    AnnoRefl::deserialize(enum_test2, archive);

    assert(enum_test2.m_color == EnumTest::Color::Blue);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationEnumTest();
    return 0;
}

#include "__generated__/serialization_enum.h.inc"
