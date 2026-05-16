#include "serialization_array_ptr.h"

#include "meta_serialization_array_ptr/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationArrayPtrTest() {
    using namespace SerializationTest;

    ArrayPtrTest array_ptr_test;
    for (int i = 0; i < 3; i++) array_ptr_test.m_array[i] = i * 100 + 1;

    auto base_data_ptr = std::make_shared<BaseData>();
    for (int i = 0; i < 3; i++) base_data_ptr->data[i] = 100.0f * i;
    array_ptr_test.m_ptr_array[0][0] = base_data_ptr;

    auto inherit_data_ptr = std::make_shared<InheritTest>();
    for (int i = 0; i < 3; i++) inherit_data_ptr->data[i] = 10.0f * i;
    inherit_data_ptr->m_inherit = 1000;
    array_ptr_test.m_ptr_array[1][1] = inherit_data_ptr;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(array_ptr_test, archive);

    ArrayPtrTest array_ptr_test2;
    Engine::Serialization::deserialize(array_ptr_test2, archive);

    for (int i = 0; i < 3; i++) assert(array_ptr_test2.m_array[i] == i * 100 + 1);
    for (int i = 0; i < 3; i++) assert(array_ptr_test2.m_ptr_array[0][0]->data[i] == 100.0f * i);

    auto inherit_data_ptr2 = std::dynamic_pointer_cast<InheritTest>(array_ptr_test2.m_ptr_array[1][1]);
    for (int i = 0; i < 3; i++) assert(inherit_data_ptr2->data[i] == 10.0f * i);
    assert(inherit_data_ptr2->m_inherit == 1000);
    assert(array_ptr_test2.m_ptr_array[0][1] == nullptr);
    assert(array_ptr_test2.m_ptr_array[1][0] == nullptr);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationArrayPtrTest();
    return 0;
}

#include "__generated__/serialization_array_ptr.h.inc"
