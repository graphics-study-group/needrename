#include "serialization_any.h"

#include "meta_annorefl_serialization_any/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <AnnoRefl/serialization.h>
#include <AnnoRefl/serialization_any.h>
#include <AnnoRefl/serialization_smart_pointer.h>
#include <AnnoRefl/serialization_vector.h>
#include <any>
#include <cassert>
#include <string>

namespace {
    void InitializeSerializationRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationAnyTest() {
    using namespace SerializationTest;

    AnnoRefl::Archive archive;
    std::shared_ptr<StdAnyTest> std_any_test = std::make_shared<StdAnyTest>();
    std_any_test->m_any_vector.push_back(1);
    std_any_test->m_any_vector.push_back(2.0f);
    std_any_test->m_any_vector.push_back(std::string("Hello World!"));

    AnnoRefl::serialize(std_any_test, archive);

    std_any_test->m_any_vector.clear();
    std::shared_ptr<StdAnyTest> std_any_test2 = std::make_shared<StdAnyTest>();
    AnnoRefl::deserialize(std_any_test2, archive);

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
