#include "serialization_vector.h"

#include "meta_serialization_vector/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationVectorTest() {
    using namespace SerializationTest;

    VectorTest vector_test;
    for (int i = 0; i < 3; i++) {
        BaseData base_data;
        for (int j = 0; j < 3; j++) {
            base_data.data[j] = 100 * j + i * 3;
        }
        vector_test.m_vector.push_back(base_data);
    }

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(vector_test, archive);

    vector_test.m_vector.clear();
    VectorTest vector_test2;
    Engine::Serialization::deserialize(vector_test2, archive);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            assert(vector_test2.m_vector[i].data[j] == 100 * j + i * 3);
        }
    }
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationVectorTest();
    return 0;
}

#include "__generated__/serialization_vector.h.inc"
