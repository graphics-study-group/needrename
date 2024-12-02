#include <iostream>
#include "serialization_test.h"
#include "meta_serialization_test/reflection_init.ipp"

int main()
{
    Engine::Reflection::Initialize();
    RegisterAllTypes();

    using namespace SerializationTest;
    std::shared_ptr<BaseData> base_data_ptr = std::make_shared<BaseData>();
    for(int i = 0; i < 3; i++)
    {
        base_data_ptr->data[i] = 182.376f * i;
    }

    Engine::Serialization::Archive archive;

    InheritTest inherit_test;
    for(int i = 0; i < 3; i++)
    {
        inherit_test.data[i] = 182.376f * i;
    }
    inherit_test.m_inherit = 1000;

    archive.clear();
    Engine::Serialization::serialize(inherit_test, archive);
    std::cout << "inherit test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    inherit_test.data[0] = 0;
    inherit_test.m_inherit = 0;
    InheritTest inherit_test2;
    Engine::Serialization::deserialize(inherit_test2, archive);
    archive.clear();
    for(int i = 0; i < 3; i++)
        assert(inherit_test2.data[i] == 182.376f * i);
    assert(inherit_test2.m_inherit == 1000);

    SharedPtrTest shared_ptr_test;
    shared_ptr_test.m_shared_ptr = base_data_ptr;
    shared_ptr_test.m_shared_ptr2 = shared_ptr_test.m_shared_ptr;
    archive.clear();
    Engine::Serialization::serialize(shared_ptr_test, archive);
    std::cout << "shared ptr test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    shared_ptr_test.m_shared_ptr.reset();
    SharedPtrTest shared_ptr_test2;
    Engine::Serialization::deserialize(shared_ptr_test2, archive);
    archive.clear();
    for(int i = 0; i < 3; i++)
        assert(shared_ptr_test2.m_shared_ptr->data[i] == 182.376f * i);
    assert(shared_ptr_test2.m_shared_ptr2.get() == shared_ptr_test2.m_shared_ptr.get());

    VectorTest vector_test;
    for(int i = 0; i < 3; i++)
    {
        BaseData base_data;
        for(int j = 0; j < 3; j++)
        {
            base_data.data[j] = 100 * j + i * 3;
        }
        vector_test.m_vector.push_back(base_data);
    }
    archive.clear();
    Engine::Serialization::serialize(vector_test, archive);
    std::cout << "vector test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    vector_test.m_vector.clear();
    VectorTest vector_test2;
    Engine::Serialization::deserialize(vector_test2, archive);
    archive.clear();
    for(int i = 0; i < 3; i++)
    {
        for(int j = 0; j < 3; j++)
        {
            assert(vector_test2.m_vector[i].data[j] == 100 * j + i * 3);
        }
    }

    CustomTest custom_test;
    custom_test.m_a = 123;
    custom_test.m_b = 852765;
    archive.clear();
    Engine::Serialization::serialize(custom_test, archive);
    std::cout << "custom test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    custom_test.m_a = 0;
    custom_test.m_b = 0;
    CustomTest custom_test2;
    Engine::Serialization::deserialize(custom_test2, archive);
    archive.clear();
    assert(custom_test2.m_a == 123);
    assert(custom_test2.m_b == 852765);

    PolymorphismTest polymorphism_test;
    auto base_data_ptr2 = std::make_shared<BaseData>();
    for(int i = 0; i < 3; i++)
        base_data_ptr2->data[i] = 100.0f * i;
    polymorphism_test.m_vector.push_back(base_data_ptr2);
    auto inherit_data_ptr = std::make_shared<InheritTest>();
    for(int i = 0; i < 3; i++)
        inherit_data_ptr->data[i] = 10.0f * i;
    inherit_data_ptr->m_inherit = 1000;
    polymorphism_test.m_vector.push_back(inherit_data_ptr);
    archive.clear();
    Engine::Serialization::serialize(polymorphism_test, archive);
    std::cout << "polymorphism test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    polymorphism_test.m_vector.clear();
    PolymorphismTest polymorphism_test2;
    Engine::Serialization::deserialize(polymorphism_test2, archive);
    archive.clear();
    assert(polymorphism_test2.m_vector.size() == 2);
    for(int i = 0; i < 3; i++)
        assert(polymorphism_test2.m_vector[0]->data[i] == 100.0f * i);
    auto inherit_data_ptr2 = std::dynamic_pointer_cast<InheritTest>(polymorphism_test2.m_vector[1]);
    assert(inherit_data_ptr2);
    for(int i = 0; i < 3; i++)
        assert(inherit_data_ptr2->data[i] == 10.0f * i);
    assert(inherit_data_ptr2->m_inherit == 1000);

    ArrayPtrTest array_ptr_test;
    for(int i = 0; i < 3; i++)
        array_ptr_test.m_array[i] = i * 100 + 1;
    base_data_ptr2 = std::make_shared<BaseData>();
    for(int i = 0; i < 3; i++)
        base_data_ptr2->data[i] = 100.0f * i;
    array_ptr_test.m_ptr_array[0][0] = base_data_ptr2;
    inherit_data_ptr = std::make_shared<InheritTest>();
    for(int i = 0; i < 3; i++)
        inherit_data_ptr->data[i] = 10.0f * i;
    inherit_data_ptr->m_inherit = 1000;
    array_ptr_test.m_ptr_array[1][1] = inherit_data_ptr;
    archive.clear();
    Engine::Serialization::serialize(array_ptr_test, archive);
    std::cout << "array ptr test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    ArrayPtrTest array_ptr_test2;
    Engine::Serialization::deserialize(array_ptr_test2, archive);
    archive.clear();
    for(int i = 0; i < 3; i++)
        assert(array_ptr_test2.m_array[i] == i * 100 + 1);
    for(int i = 0; i < 3; i++)
        assert(array_ptr_test2.m_ptr_array[0][0]->data[i] == 100.0f * i);
    auto inherit_data_ptr3 = dynamic_pointer_cast<InheritTest>(array_ptr_test2.m_ptr_array[1][1]);
    for(int i = 0; i < 3; i++)
        assert(inherit_data_ptr3->data[i] == 10.0f * i);
    assert(inherit_data_ptr3->m_inherit == 1000);
    assert(array_ptr_test2.m_ptr_array[0][1] == nullptr);
    assert(array_ptr_test2.m_ptr_array[1][0] == nullptr);

    return 0;
}
