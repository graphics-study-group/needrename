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

    InheritTest inherit_test;
    for(int i = 0; i < 3; i++)
    {
        inherit_test.data[i] = 182.376f * i;
    }
    inherit_test.m_inherit = 1000;

    Engine::Serialization::Archive buffer;
    // Engine::Serialization::save(base_data, buffer);
    // std::cout << buffer.json.dump(4) << std::endl;
    buffer.clear();
    Engine::Serialization::serialize(inherit_test, buffer);
    std::cout << "inherit test:" << std::endl << buffer.json.dump(4) << std::endl;

    SharedPtrTest shared_ptr_test;
    shared_ptr_test.m_shared_ptr = base_data_ptr;

    buffer.clear();
    Engine::Serialization::serialize(shared_ptr_test, buffer);
    std::cout << "shared ptr test:" << std::endl << buffer.json.dump(4) << std::endl;

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
    
    CustomTest custom_test;
    custom_test.m_a = 621;
    custom_test.m_b = 182376;

    buffer.clear();
    Engine::Serialization::serialize(custom_test, buffer);
    std::cout << "custom test:" << std::endl << buffer.json.dump(4) << std::endl;

    return 0;
}
