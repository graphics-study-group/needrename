#include "serialization_self_ptr.h"

#include "meta_serialization_self_ptr/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationSelfPtrTest() {
    using namespace SerializationTest;

    std::shared_ptr<SelfPtrTest> self_ptr_test = std::make_shared<SelfPtrTest>();
    self_ptr_test->m_self_ptr = self_ptr_test;

    Engine::Serialization::Archive archive;
    Engine::Serialization::serialize(self_ptr_test, archive);

    self_ptr_test->m_self_ptr.reset();
    std::shared_ptr<SelfPtrTest> self_ptr_test2 = std::make_shared<SelfPtrTest>();
    Engine::Serialization::deserialize(self_ptr_test2, archive);

    assert(self_ptr_test2->m_self_ptr.get() == self_ptr_test2.get());
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationSelfPtrTest();
    return 0;
}

#include "__generated__/serialization_self_ptr.h.inc"
