#include "serialization_any.h"

#include "meta_serialization_any/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <any>
#include <cassert>
#include <string>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationAnyTest() {
    using namespace SerializationTest;

    Engine::Serialization::Archive archive;
    std::shared_ptr<StdAnyTest> std_any_test = std::make_shared<StdAnyTest>();
    std_any_test->m_any_vector.push_back(1);
    std_any_test->m_any_vector.push_back(2.0f);
    std_any_test->m_any_vector.push_back(std::string("Hello World!"));

    Engine::Serialization::serialize(std_any_test, archive);

    std_any_test->m_any_vector.clear();
    std::shared_ptr<StdAnyTest> std_any_test2 = std::make_shared<StdAnyTest>();
    Engine::Serialization::deserialize(std_any_test2, archive);

    assert(std::any_cast<int>(std_any_test2->m_any_vector[0]) == 1);
    assert(std::any_cast<float>(std_any_test2->m_any_vector[1]) == 2.0f);
    assert(std::any_cast<std::string>(std_any_test2->m_any_vector[2]) == "Hello World!");
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationAnyTest();
    return 0;
}

#include "__generated__/serialization_any.h.inc"
