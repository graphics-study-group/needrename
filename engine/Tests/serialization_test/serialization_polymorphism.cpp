#include "serialization_polymorphism.h"

#include "meta_serialization_polymorphism/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationPolymorphismTest() {
    using namespace SerializationTest;

    PolymorphismTest polymorphism_test;
    auto base_data_ptr = std::make_shared<BaseData>();
    for (int i = 0; i < 3; i++) base_data_ptr->data[i] = 100.0f * i;
    polymorphism_test.m_vector.push_back(base_data_ptr);

    auto inherit_data_ptr = std::make_shared<InheritTest>();
    for (int i = 0; i < 3; i++) inherit_data_ptr->data[i] = 10.0f * i;
    inherit_data_ptr->m_inherit = 1000;
    polymorphism_test.m_vector.push_back(inherit_data_ptr);

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(polymorphism_test, archive);

    polymorphism_test.m_vector.clear();
    PolymorphismTest polymorphism_test2;
    Engine::Serialization::deserialize(polymorphism_test2, archive);

    assert(polymorphism_test2.m_vector.size() == 2);
    for (int i = 0; i < 3; i++) assert(polymorphism_test2.m_vector[0]->data[i] == 100.0f * i);
    auto inherit_data_ptr2 = std::dynamic_pointer_cast<InheritTest>(polymorphism_test2.m_vector[1]);
    assert(inherit_data_ptr2);
    for (int i = 0; i < 3; i++) assert(inherit_data_ptr2->data[i] == 10.0f * i);
    assert(inherit_data_ptr2->m_inherit == 1000);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationPolymorphismTest();
    return 0;
}

#include "__generated__/serialization_polymorphism.h.inc"
