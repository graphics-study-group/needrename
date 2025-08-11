#include "reflection_test.h"
#include "meta_reflection_test/reflection_init.ipp"
#include <Reflection/reflection.h>
#include <cassert>
#include <iostream>

const void *FooBase_PrintHelloWorld_Called = nullptr;
void FooBase::PrintHelloWorld() const {
    FooBase_PrintHelloWorld_Called = this;
    std::cout << "Hello World from FooBase!" << std::endl;
}

void *BBase_PrintB_Called = nullptr;
void BBase::PrintB() {
    BBase_PrintB_Called = this;
    std::cout << "PrintB from BBase!" << std::endl;
}

void *FooA_FooA_Called = nullptr;
FooA::FooA(int a, int b) : m_a(a), m_b(b) {
    FooA_FooA_Called = this;
}

void *FooA_PrintInfo_Called = nullptr;
void FooA::PrintInfo() {
    FooA_PrintInfo_Called = this;
    std::cout << "FooA: Hahahaha! " << "m_a: " << m_a << " m_b: " << m_b << std::endl;
}

void *FooA_Add2_Called = nullptr;
int FooA::Add(int a, int b) {
    FooA_Add2_Called = this;
    return a + b + m_a + m_b;
}

void *FooA_Add3_Called = nullptr;
int FooA::Add(int a, int b, int c) {
    FooA_Add3_Called = this;
    return a + b + c + m_a + m_b;
}

const void *FooA_PrintHelloWorld_Called = nullptr;
void FooA::PrintHelloWorld() const {
    FooA_PrintHelloWorld_Called = this;
    std::cout << "Hello World from FooA!" << std::endl;
}

using namespace TestDataNamespace;
const void *ConstTest_GetConstDataPtr_Called = nullptr;
const TestData *ConstTest::GetConstDataPtr() const {
    ConstTest_GetConstDataPtr_Called = this;
    return m_const_data;
}

void *ConstTest_SetConstDataPtr_Called = nullptr;
void ConstTest::SetConstDataPtr(const TestData *data) {
    ConstTest_SetConstDataPtr_Called = this;
    m_const_data = data;
}

void *ConstTest_SetConstDataRef_Called = nullptr;
void ConstTest::SetConstDataRef(const TestData &data) {
    ConstTest_SetConstDataRef_Called = this;
    m_const_data = &data;
}

const void *ConstTest_GetConstDataRef_Called = nullptr;
const TestData &ConstTest::GetConstDataRef() const {
    ConstTest_GetConstDataRef_Called = this;
    return *m_const_data;
}

const void *ConstTest_GetTestDataPtrAndAdd_Called = nullptr;
const TestData *ConstTest::GetTestDataPtrAndAdd() {
    ConstTest_GetTestDataPtrAndAdd_Called = this;
    m_data->data[0] += 1.0f;
    return m_const_data;
}

void *ConstTest_GetTestDataPtr_Called = nullptr;
TestData *ConstTest::GetTestDataPtr() {
    ConstTest_GetTestDataPtr_Called = this;
    return m_data;
}

void *ConstTest_GetTestDataRef_Called = nullptr;
TestData &ConstTest::GetTestDataRef() {
    ConstTest_GetTestDataRef_Called = this;
    return *m_data;
}

const void *NamespaceTest_PrintInfo_Called = nullptr;
void NamespaceTest::PrintInfo() const {
    NamespaceTest_PrintInfo_Called = this;
    std::cout << "NamespaceTest: Hahahaha!" << std::endl;
}

const void *TestHelloWorld_NamespaceTest_PrintInfo_Called = nullptr;
void TestHelloWorld::NamespaceTest::PrintInfo() const {
    TestHelloWorld_NamespaceTest_PrintInfo_Called = this;
    std::cout << "TestHelloWorld::NamespaceTest: Hahahaha!" << std::endl;
}

const void *TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called = nullptr;
void TestHelloWorld::TestHelloWorld2::NamespaceTest::PrintInfo() const {
    TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called = this;
    std::cout << "TestHelloWorld::TestHelloWorld2::NamespaceTest: Hahahaha!" << std::endl;
}

int main() {
    Engine::Reflection::Initialize();
    RegisterAllTypes();

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

    auto FooAtype = Engine::Reflection::GetType("FooA");
    std::cout << "----------------------------------- Test a FooA var -----------------------------------" << std::endl;
    std::cout << "[] (FooA) Var foo:" << std::endl;
    Engine::Reflection::Var foo = FooAtype->CreateInstance(123, 456);
    void *foo_data = foo.GetDataPtr();
    assert(foo_data == FooA_FooA_Called);
    std::cout << "[] foo: PrintInfo" << std::endl;
    foo.InvokeMethod("PrintInfo");
    assert(foo_data == FooA_PrintInfo_Called);
    int sum = foo.InvokeMethod("Add", 1, 2).Get<int>();
    std::cout << "Sum 2 args: " << sum << std::endl;
    assert(foo_data == FooA_Add2_Called);
    assert(sum == 123 + 456 + 1 + 2);
    sum = foo.InvokeMethod("Add", 1, 2, 3).Get<int>();
    std::cout << "Sum 3 args: " << sum << std::endl;
    assert(foo_data == FooA_Add3_Called);
    assert(sum == 123 + 456 + 1 + 2 + 3);
    std::cout << "Type of foo: " << foo.m_type->GetName() << std::endl;
    assert(foo.m_type->GetName() == "FooA");
    std::cout << "foo.m_a == " << foo.GetMember("m_a").Get<int>() << std::endl;
    assert(foo.GetMember("m_a").Get<int>() == 123);
    std::cout << "[] Set foo.m_a = 100" << std::endl;
    foo.GetMember("m_a").Set(100);
    std::cout << "foo.m_a == " << foo.GetMember("m_a").Get<int>() << std::endl;
    assert(foo.GetMember("m_a").Get<int>() == 100);
    std::cout << "[] Base class function: PrintB" << std::endl;
    foo.InvokeMethod("PrintB");
    assert(foo_data == BBase_PrintB_Called);

    std::cout << "----------------------------------- Test a FooBase var -----------------------------------"
              << std::endl;
    std::cout << "[] (FooBase) Var foo_base:" << std::endl;
    Engine::Reflection::Var foo_base = Engine::Reflection::GetType("FooBase")->CreateInstance();
    void *foo_base_data = foo_base.GetDataPtr();
    std::cout << "[] FooBase: PrintHelloWorld" << std::endl;
    foo_base.InvokeMethod("PrintHelloWorld");
    assert(foo_base_data == FooBase_PrintHelloWorld_Called);

    std::cout << "----------------------------------- Test a FooA as a FooBase* var -----------------------------------"
              << std::endl;
    std::cout << "[] (Foobase) Var foo2: (it is a FooA in fact)" << std::endl;
    Engine::Reflection::Var foo2(Engine::Reflection::GetType("FooBase"), FooAtype->CreateInstance(5, 7).GetDataPtr());
    void *foo2_data = foo2.GetDataPtr();
    std::cout << "[] foo2: PrintHelloWorld (virtual)" << std::endl;
    foo2.InvokeMethod("PrintHelloWorld");
    assert(foo2_data == FooA_PrintHelloWorld_Called);

    std::cout << "----------------------------------- Test some const, ref, pointer -----------------------------------"
              << std::endl;
    TestData data1, data2, data3;
    data1.data[0] = 1.0f;
    data2.data[0] = 100.0f;
    data3.data[0] = 1000.0f;
    Engine::Reflection::Var crp_test = Engine::Reflection::GetType("TestDataNamespace::ConstTest")->CreateInstance();
    crp_test.InvokeMethod("SetConstDataPtr", (const TestData *)&data1);
    crp_test.GetMember("m_data").Set(&data2);
    std::cout << "crp_test: m_const_data[0] == " << crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0]
              << std::endl;
    assert(crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0] == 1.0f);
    for (int i = 1; i < 100; i++) assert(crp_test.GetMember("m_const_data").Get<const TestData *>()->data[i] == 0.0f);

    std::cout << "crp_test: m_data[0] == " << crp_test.GetMember("m_data").Get<TestData *>()->data[0] << std::endl;
    assert(crp_test.GetMember("m_data").Get<TestData *>()->data[0] == 100.0f);
    for (int i = 1; i < 100; i++) assert(crp_test.GetMember("m_data").Get<TestData *>()->data[i] == 0.0f);

    std::cout << "[] crp_test: GetTestDataPtrAndAdd" << std::endl;
    Engine::Reflection::Var data_get = crp_test.InvokeMethod("GetTestDataPtrAndAdd");
    std::cout << "crp_test: m_data[0] == " << crp_test.GetMember("m_data").Get<TestData *>()->data[0] << std::endl;
    assert(crp_test.GetMember("m_data").Get<TestData *>()->data[0] == 101.0f);
    for (int i = 1; i < 100; i++) assert(crp_test.GetMember("m_data").Get<TestData *>()->data[i] == 0.0f);

    std::cout << "data_get: m_data[0] == " << data_get.Get<TestData *>()->data[0] << std::endl;
    assert(data_get.Get<TestData *>()->data[0] == 1.0f);
    for (int i = 1; i < 100; i++) assert(data_get.Get<TestData *>()->data[i] == 0.0f);

    std::cout << "[] crp_test: SetTestDataPtr(data3)" << std::endl;
    crp_test.InvokeMethod("SetConstDataRef", data3);
    std::cout << "crp_test: m_const_data[0] == " << crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0]
              << std::endl;
    assert(crp_test.GetMember("m_const_data").Get<const TestData *>()->data[0] == 1000.0f);
    for (int i = 1; i < 100; i++) assert(crp_test.GetMember("m_const_data").Get<const TestData *>()->data[i] == 0.0f);

    std::cout << "crp_test (GetConstDataRef): m_const_data[0] == "
              << crp_test.InvokeMethod("GetConstDataRef").Get<const TestData &>().data[0] << std::endl;
    assert(crp_test.InvokeMethod("GetConstDataRef").Get<const TestData &>().data[0] == 1000.0f);
    for (int i = 1; i < 100; i++)
        assert(crp_test.InvokeMethod("GetConstDataRef").Get<const TestData &>().data[i] == 0.0f);

    std::cout << "[] Const Var (const ConstTest) const_ref_var:" << std::endl;
    const ConstTest &const_ref = crp_test.Get<ConstTest>();
    Engine::Reflection::ConstVar const_ref_var = Engine::Reflection::GetConstVar(const_ref);
    Engine::Reflection::ConstVar const_data_get = const_ref_var.InvokeMethod("GetConstDataPtr");
    std::cout << "const_data_get: m_data[0] == " << const_data_get.Get<const TestData *>()->data[0] << std::endl;
    assert(const_data_get.Get<const TestData *>()->data[0] == 1000.0f);
    for (int i = 1; i < 100; i++) assert(const_data_get.Get<const TestData *>()->data[i] == 0.0f);

    std::cout << "[] make data3 +1.0f" << std::endl;
    data3.data[0] += 1.0f;
    std::cout << "const_data_get: m_data[0] == " << const_data_get.Get<const TestData *>()->data[0] << std::endl;
    assert(const_data_get.Get<const TestData *>()->data[0] == 1001.0f);
    for (int i = 1; i < 100; i++) assert(const_data_get.Get<const TestData *>()->data[i] == 0.0f);

    std::cout << "----------------------------------- Test namespace of classes -----------------------------------"
              << std::endl;
    Engine::Reflection::Var ns_test = Engine::Reflection::GetType("NamespaceTest")->CreateInstance();
    ns_test.InvokeMethod("PrintInfo");
    assert(ns_test.GetDataPtr() == NamespaceTest_PrintInfo_Called);
    Engine::Reflection::Var ns_test2 = Engine::Reflection::GetType("TestHelloWorld::NamespaceTest")->CreateInstance();
    ns_test2.InvokeMethod("PrintInfo");
    assert(ns_test2.GetDataPtr() == TestHelloWorld_NamespaceTest_PrintInfo_Called);
    Engine::Reflection::Var ns_test3 =
        Engine::Reflection::GetType("TestHelloWorld::TestHelloWorld2::NamespaceTest")->CreateInstance();
    ns_test3.InvokeMethod("PrintInfo");
    assert(ns_test3.GetDataPtr() == TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called);

    return 0;
}
