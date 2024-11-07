#include <iostream>
#include "serialization_test.h"
#include "Reflection/serialization.h"

int main()
{
    using namespace SerializationTest;
    BaseData base_data;
    for(int i = 0; i < 3; i++)
    {
        base_data.data[i] = 182.376f * i;
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
    std::cout << buffer.json.dump(4) << std::endl;
    
    CustomTest custom_test;
    custom_test.m_a = 621;
    custom_test.m_b = 182376;

    buffer.clear();
    Engine::Serialization::serialize(custom_test, buffer);
    std::cout << buffer.json.dump(4) << std::endl;

    return 0;
}
