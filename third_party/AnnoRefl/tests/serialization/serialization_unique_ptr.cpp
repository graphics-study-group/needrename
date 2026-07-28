#include "serialization_unique_ptr.h"

#include "meta_annorefl_serialization_unique_ptr/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <AnnoRefl/serialization.h>
#include <AnnoRefl/serialization_smart_pointer.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationUniquePtrTest() {
    using namespace SerializationTest;

    UniquePtrTest unique_ptr_test;
    unique_ptr_test.m_unique_ptr = std::make_unique<BaseData>();
    for (int i = 0; i < 3; i++) {
        unique_ptr_test.m_unique_ptr->data[i] = 182.376f * i;
    }

    AnnoRefl::Archive archive;
    AnnoRefl::serialize(unique_ptr_test, archive);

    unique_ptr_test.m_unique_ptr.reset();
    UniquePtrTest unique_ptr_test2;
    AnnoRefl::deserialize(unique_ptr_test2, archive);

    for (int i = 0; i < 3; i++) assert(unique_ptr_test2.m_unique_ptr->data[i] == 182.376f * i);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationUniquePtrTest();
    return 0;
}

#include "__generated__/serialization_unique_ptr.h.inc"
