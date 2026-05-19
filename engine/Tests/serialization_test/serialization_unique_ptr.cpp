#include "serialization_unique_ptr.h"

#include "meta_serialization_unique_ptr/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
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

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(unique_ptr_test, archive);

    unique_ptr_test.m_unique_ptr.reset();
    UniquePtrTest unique_ptr_test2;
    Engine::Serialization::deserialize(unique_ptr_test2, archive);

    for (int i = 0; i < 3; i++) assert(unique_ptr_test2.m_unique_ptr->data[i] == 182.376f * i);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationUniquePtrTest();
    return 0;
}

#include "__generated__/serialization_unique_ptr.h.inc"
