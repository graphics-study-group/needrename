#include "serialization_stdint.h"
#include "meta_serialization_stdint/reflection_init.inc"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <cassert>
#include <iostream>

namespace {
    void InitializeSerializationRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

namespace {
    void RunSerializationStdintTestImpl() {
        using namespace SerializationTest;

        Engine::Serialization::Archive archive;

        std::shared_ptr<StdintTest> stdint_test = std::make_shared<StdintTest>();
        stdint_test->m_int8 = 1;
        stdint_test->m_int16 = 2;
        stdint_test->m_int32 = 3;
        stdint_test->m_int64 = 4;
        stdint_test->m_uint8 = 5;
        stdint_test->m_uint16 = 6;
        stdint_test->m_uint32 = 7;
        stdint_test->m_uint64 = 8;
        Engine::Serialization::serialize(stdint_test, archive);
        std::cout << "stdint test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
        stdint_test->m_int8 = 0;
        stdint_test->m_int16 = 0;
        stdint_test->m_int32 = 0;
        stdint_test->m_int64 = 0;
        stdint_test->m_uint8 = 0;
        stdint_test->m_uint16 = 0;
        stdint_test->m_uint32 = 0;
        stdint_test->m_uint64 = 0;
        std::shared_ptr<StdintTest> stdint_test2 = std::make_shared<StdintTest>();
        Engine::Serialization::deserialize(stdint_test2, archive);
        archive.clear();
        assert(stdint_test2->m_int8 == 1);
        assert(stdint_test2->m_int16 == 2);
        assert(stdint_test2->m_int32 == 3);
        assert(stdint_test2->m_int64 == 4);
        assert(stdint_test2->m_uint8 == 5);
        assert(stdint_test2->m_uint16 == 6);
        assert(stdint_test2->m_uint32 == 7);
        assert(stdint_test2->m_uint64 == 8);
    }
} // namespace

void RunSerializationStdintTest() {
    RunSerializationStdintTestImpl();
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationStdintTest();
    return 0;
}

#include "__generated__/serialization_stdint.h.inc"
