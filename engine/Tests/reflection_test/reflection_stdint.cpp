#include "reflection_stdint.h"
#include "meta_reflection_stdint/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>
#include <iostream>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

namespace {
    void RunReflectionStdintTestImpl() {
        auto stdint_type = Engine::Reflection::GetType("Test_stdint");
        std::cout << "----------------------------------- Test stdint -----------------------------------" << std::endl;
        std::cout << "[] (Test_stdint) Var stdint:" << std::endl;
        Engine::Reflection::Var stdint = stdint_type->CreateInstance();
        stdint.GetMember("m_int8").Set((int8_t)1);
        stdint.GetMember("m_int16").Set((int16_t)2);
        stdint.GetMember("m_int32").Set((int32_t)3);
        stdint.GetMember("m_int64").Set((int64_t)4);
        stdint.GetMember("m_uint8").Set((uint8_t)5);
        stdint.GetMember("m_uint16").Set((uint16_t)6);
        stdint.GetMember("m_uint32").Set((uint32_t)7);
        stdint.GetMember("m_uint64").Set((uint64_t)8);
        std::cout << "m_int8: " << (int)stdint.GetMember("m_int8").Get<int8_t>() << std::endl;
        assert(stdint.GetMember("m_int8").Get<int8_t>() == 1);
        std::cout << "m_int16: " << stdint.GetMember("m_int16").Get<int16_t>() << std::endl;
        assert(stdint.GetMember("m_int16").Get<int16_t>() == 2);
        std::cout << "m_int32: " << stdint.GetMember("m_int32").Get<int32_t>() << std::endl;
        assert(stdint.GetMember("m_int32").Get<int32_t>() == 3);
        std::cout << "m_int64: " << stdint.GetMember("m_int64").Get<int64_t>() << std::endl;
        assert(stdint.GetMember("m_int64").Get<int64_t>() == 4);
        std::cout << "m_uint8: " << (unsigned int)stdint.GetMember("m_uint8").Get<uint8_t>() << std::endl;
        assert(stdint.GetMember("m_uint8").Get<uint8_t>() == 5);
        std::cout << "m_uint16: " << stdint.GetMember("m_uint16").Get<uint16_t>() << std::endl;
        assert(stdint.GetMember("m_uint16").Get<uint16_t>() == 6);
        std::cout << "m_uint32: " << stdint.GetMember("m_uint32").Get<uint32_t>() << std::endl;
        assert(stdint.GetMember("m_uint32").Get<uint32_t>() == 7);
        std::cout << "m_uint64: " << stdint.GetMember("m_uint64").Get<uint64_t>() << std::endl;
        assert(stdint.GetMember("m_uint64").Get<uint64_t>() == 8);
    }
} // namespace

void RunReflectionStdintTest() {
    RunReflectionStdintTestImpl();
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionStdintTest();
    return 0;
}

#include "__generated__/reflection_stdint.h.inc"
