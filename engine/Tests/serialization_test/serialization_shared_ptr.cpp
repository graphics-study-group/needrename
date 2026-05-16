#include "serialization_shared_ptr.h"

#include "meta_serialization_shared_ptr/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationSharedPtrTest() {
    using namespace SerializationTest;

    auto base_data_ptr = std::make_shared<BaseData>();
    for (int i = 0; i < 3; i++) {
        base_data_ptr->data[i] = 182.376f * i;
    }

    SharedPtrTest shared_ptr_test;
    shared_ptr_test.m_shared_ptr = base_data_ptr;
    shared_ptr_test.m_shared_ptr2 = shared_ptr_test.m_shared_ptr;
    shared_ptr_test.m_int_ptr = std::make_shared<int>(621);
    shared_ptr_test.m_weak_ptr = shared_ptr_test.m_shared_ptr;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(shared_ptr_test, archive);

    shared_ptr_test.m_shared_ptr.reset();
    SharedPtrTest shared_ptr_test2;
    Engine::Serialization::deserialize(shared_ptr_test2, archive);

    for (int i = 0; i < 3; i++) assert(shared_ptr_test2.m_shared_ptr->data[i] == 182.376f * i);
    assert(shared_ptr_test2.m_shared_ptr2.get() == shared_ptr_test2.m_shared_ptr.get());
    assert(shared_ptr_test2.m_weak_ptr.lock().get() == shared_ptr_test2.m_shared_ptr.get());
    assert(*shared_ptr_test2.m_int_ptr == 621);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationSharedPtrTest();
    return 0;
}

#include "__generated__/serialization_shared_ptr.h.inc"
