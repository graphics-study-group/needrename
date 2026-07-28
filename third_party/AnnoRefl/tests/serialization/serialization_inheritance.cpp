#include "serialization_inheritance.h"

#include "meta_annorefl_serialization_inheritance/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <AnnoRefl/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        AnnoRefl::Initialize();
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

    AnnoRefl::Archive archive;
    AnnoRefl::serialize(inherit_test, archive);

    inherit_test.data[0] = 0;
    inherit_test.m_inherit = 0;

    InheritTest inherit_test2;
    AnnoRefl::deserialize(inherit_test2, archive);

    for (int i = 0; i < 3; i++) assert(inherit_test2.data[i] == 182.376f * i);
    assert(inherit_test2.m_inherit == 1000);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationInheritanceTest();
    return 0;
}

#include "__generated__/serialization_inheritance.h.inc"
