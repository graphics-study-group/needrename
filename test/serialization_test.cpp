#include <iostream>
#include "serialization_test.h"
#include "Reflection/serialization.h"

int main()
{
    using namespace SerializationTest;
    std::shared_ptr<BaseData> base_data_ptr = std::make_shared<BaseData>();
    for(int i = 0; i < 3; i++)
    {
        base_data_ptr->data[i] = 182.376f * i;
    }

    Engine::Serialization::Archive buffer;

    InheritTest inherit_test;
    for(int i = 0; i < 3; i++)
    {
        inherit_test.data[i] = 182.376f * i;
    }
    inherit_test.m_inherit = 1000;
    // Engine::Serialization::save(base_data, buffer);
    // std::cout << buffer.json.dump(4) << std::endl;
    buffer.clear();
    Engine::Serialization::serialize(inherit_test, buffer);
    std::cout << "inherit test:" << std::endl << buffer.json.dump(4) << std::endl;
    inherit_test.data[0] = 0;
    inherit_test.m_inherit = 0;
    InheritTest inherit_test2;
    Engine::Serialization::deserialize(inherit_test2, buffer);
    for(int i = 0; i < 3; i++)
        assert(inherit_test2.data[i] == 182.376f * i);
    assert(inherit_test2.m_inherit == 1000);

    SharedPtrTest shared_ptr_test;
    shared_ptr_test.m_shared_ptr = base_data_ptr;
    buffer.clear();
    Engine::Serialization::serialize(shared_ptr_test, buffer);
    std::cout << "shared ptr test:" << std::endl << buffer.json.dump(4) << std::endl;
    shared_ptr_test.m_shared_ptr.reset();
    SharedPtrTest shared_ptr_test2;
    Engine::Serialization::deserialize(shared_ptr_test2, buffer);
    for(int i = 0; i < 3; i++)
        assert(shared_ptr_test2.m_shared_ptr->data[i] == 182.376f * i);

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
    buffer.clear();
    Engine::Serialization::serialize(vector_test, buffer);
    std::cout << "vector test:" << std::endl << buffer.json.dump(4) << std::endl;
    vector_test.m_vector.clear();
    VectorTest vector_test2;
    Engine::Serialization::deserialize(vector_test2, buffer);
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
    buffer.clear();
    Engine::Serialization::serialize(custom_test, buffer);
    std::cout << "custom test:" << std::endl << buffer.json.dump(4) << std::endl;
    custom_test.m_a = 0;
    custom_test.m_b = 0;
    CustomTest custom_test2;
    Engine::Serialization::deserialize(custom_test2, buffer);
    assert(custom_test2.m_a == 123);
    assert(custom_test2.m_b == 852765);

    return 0;
}
