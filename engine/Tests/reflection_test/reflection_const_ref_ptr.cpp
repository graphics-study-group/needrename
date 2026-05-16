#include "reflection_const_ref_ptr.h"

#include "meta_reflection_const_ref_ptr/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>
#include <iostream>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionConstRefPtrTest() {
    using namespace TestDataNamespace;

    std::cout << "----------------------------------- Test some const, ref, pointer -----------------------------------"
              << std::endl;
    TestData data1, data2, data3;
    data1.data[0] = 1.0f;
    data2.data[0] = 100.0f;
    data3.data[0] = 1000.0f;

    Engine::Reflection::Var crp_test = Engine::Reflection::GetType("TestDataNamespace::ConstTest")->CreateInstance();
    crp_test.InvokeMethod("SetConstDataPtr", (const TestData *)&data1);
    crp_test.GetMember("m_data").Set(&data2);
    assert(crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0] == 1.0f);
    assert(crp_test.GetMember("m_data").Get<TestData *>()->data[0] == 100.0f);

    Engine::Reflection::Var data_get = crp_test.InvokeMethod("GetTestDataPtrAndAdd");
    assert(crp_test.GetMember("m_data").Get<TestData *>()->data[0] == 101.0f);
    assert(data_get.Get<TestData *>()->data[0] == 1.0f);

    crp_test.InvokeMethod("SetConstDataRef", data3);
    assert(crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0] == 1000.0f);
    assert(crp_test.InvokeMethod("GetConstDataRef").Get<const TestData &>().data[0] == 1000.0f);

    const ConstTest &const_ref = crp_test.Get<ConstTest>();
    Engine::Reflection::Var const_ref_var = Engine::Reflection::GetVar(const_ref);
    Engine::Reflection::Var const_data_get = const_ref_var.InvokeMethod("GetConstDataPtr");
    assert(const_data_get.Get<const TestData *>()->data[0] == 1000.0f);

    data3.data[0] += 1.0f;
    assert(const_data_get.Get<const TestData *>()->data[0] == 1001.0f);

    data3.data[7] = 132.0f;
    assert(const_data_get.GetPointedVar().InvokeMethod("GetData", 7).Get<float>() == 132.0f);

    bool const_check = false;
    try {
        const_data_get.GetPointedVar().InvokeMethod("SetData", 7, 123.0f);
    } catch (const std::runtime_error &e) {
        const_check = (std::string(e.what()) == "Method SetData is not const, but the Var is const");
    }
    assert(const_check);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionConstRefPtrTest();
    return 0;
}

#include "__generated__/reflection_const_ref_ptr.h.inc"
