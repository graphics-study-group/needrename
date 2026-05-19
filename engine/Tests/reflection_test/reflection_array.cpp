#include "reflection_array.h"

#include "meta_reflection_array/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionArrayTest() {
    Engine::Reflection::Var array_test = Engine::Reflection::GetType("ArrayTest")->CreateInstance();
    array_test.GetArrayMember("m_array_int").GetElement(1).Set<int>(1);
    int *array_int2 = static_cast<int *>(array_test.GetArrayMember("m_array_int2").GetElement(2).GetDataPtr());
    array_int2[2] = 2;
    array_test.GetArrayMember("m_array_double").GetElement(4).Set<double>(4.0);

    assert(array_test.GetArrayMember("m_array_int").GetElement(1).Get<int>() == 1);
    assert(array_test.Get<ArrayTest>().m_array_int2[2][2] == 2);
    assert(array_test.GetArrayMember("m_array_double").GetElement(4).Get<double>() == 4.0);
    assert(array_test.GetArrayMember("m_array_int").GetSize() == 5);
    assert(array_test.GetArrayMember("m_array_int2").GetSize() == 9);
    assert(array_test.GetArrayMember("m_array_double").GetSize() == 12);

    assert(array_test.GetArrayMember("m_vector_float").GetSize() == 0);
    array_test.GetArrayMember("m_vector_float").Resize(3);
    array_test.GetArrayMember("m_vector_float").GetElement(0).Set<float>(1.0f);
    array_test.GetArrayMember("m_vector_float").GetElement(1).Set<float>(2.0f);
    array_test.GetArrayMember("m_vector_float").GetElement(2).Set<float>(3.0f);

    assert(array_test.GetArrayMember("m_vector_float").GetElement(0).Get<float>() == 1.0f);
    assert(array_test.GetArrayMember("m_vector_float").GetElement(1).Get<float>() == 2.0f);
    assert(array_test.GetArrayMember("m_vector_float").GetElement(2).Get<float>() == 3.0f);
    assert(array_test.GetArrayMember("m_vector_float").GetSize() == 3);

    array_test.GetArrayMember("m_vector_float").Append(40.0f);
    assert(array_test.GetArrayMember("m_vector_float").GetElement(3).Get<float>() == 40.0f);

    array_test.GetArrayMember("m_vector_float").Remove(1);
    assert(array_test.GetArrayMember("m_vector_float").GetElement(1).Get<float>() == 3.0f);
    assert(array_test.GetArrayMember("m_vector_float").GetSize() == 3);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionArrayTest();
    return 0;
}

#include "__generated__/reflection_array.h.inc"
