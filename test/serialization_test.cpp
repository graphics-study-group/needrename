#include <iostream>
#include "serialization_test.h"
#include "Reflection/serialization.h"

int main()
{
    using namespace SerializationTest;
    BaseData base_data;
    for(int i = 0; i < 100; i++)
    {
        base_data.data[i] = 1.0f * i / 10.0f;
    }

    InheritTest inherit_test;
    for(int i = 0; i < 100; i++)
    {
        inherit_test.data[i] = 1.0f * i / 10.0f;
    }
    inherit_test.m_inherit = 1000;

    Engine::Serialization::SerializedBuffer buffer;
    // Engine::Serialization::save(base_data, buffer);
    // std::cout << buffer.json.dump(4) << std::endl;
    Engine::Serialization::save(inherit_test, buffer);
    std::cout << buffer.json.dump(4) << std::endl;

    return 0;
}
