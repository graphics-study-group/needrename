#include "serialization_test.h"
#include "meta_serialization_test/reflection_init.ipp"
#include <Reflection/reflection.h>
#include <Reflection/serialization.h>
#include <iostream>

int main() {
    Engine::Reflection::Initialize();
    RegisterAllTypes();

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

    std::shared_ptr<StdAnyTest> std_any_test = std::make_shared<StdAnyTest>();
    std_any_test->m_any_vector.push_back(1);
    std_any_test->m_any_vector.push_back(2.0f);
    std_any_test->m_any_vector.push_back(std::string("Hello World!"));
    Engine::Serialization::serialize(std_any_test, archive);
    std::cout << "std any test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    std_any_test->m_any_vector.clear();
    std::shared_ptr<StdAnyTest> std_any_test2 = std::make_shared<StdAnyTest>();
    Engine::Serialization::deserialize(std_any_test2, archive);
    archive.clear();
    assert(std::any_cast<int>(std_any_test2->m_any_vector[0]) == 1);
    assert(std::any_cast<float>(std_any_test2->m_any_vector[1]) == 2.0f);
    assert(std::any_cast<std::string>(std_any_test2->m_any_vector[2]) == "Hello World!");

    std::shared_ptr<BaseData> base_data_ptr = std::make_shared<BaseData>();
    for (int i = 0; i < 3; i++) {
        base_data_ptr->data[i] = 182.376f * i;
    }
    InheritTest inherit_test;
    for (int i = 0; i < 3; i++) {
        inherit_test.data[i] = 182.376f * i;
    }
    inherit_test.m_inherit = 1000;

    archive.clear();
    Engine::Serialization::serialize(inherit_test, archive);
    std::cout << "inherit test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    inherit_test.data[0] = 0;
    inherit_test.m_inherit = 0;
    InheritTest inherit_test2;
    Engine::Serialization::deserialize(inherit_test2, archive);
    archive.clear();
    for (int i = 0; i < 3; i++) assert(inherit_test2.data[i] == 182.376f * i);
    assert(inherit_test2.m_inherit == 1000);

    SharedPtrTest shared_ptr_test;
    shared_ptr_test.m_shared_ptr = base_data_ptr;
    shared_ptr_test.m_shared_ptr2 = shared_ptr_test.m_shared_ptr;
    shared_ptr_test.m_int_ptr = std::make_shared<int>(621);
    shared_ptr_test.m_weak_ptr = shared_ptr_test.m_shared_ptr;
    archive.clear();
    Engine::Serialization::serialize(shared_ptr_test, archive);
    std::cout << "shared ptr test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    shared_ptr_test.m_shared_ptr.reset();
    SharedPtrTest shared_ptr_test2;
    Engine::Serialization::deserialize(shared_ptr_test2, archive);
    archive.clear();
    for (int i = 0; i < 3; i++) assert(shared_ptr_test2.m_shared_ptr->data[i] == 182.376f * i);
    assert(shared_ptr_test2.m_shared_ptr2.get() == shared_ptr_test2.m_shared_ptr.get());
    assert(shared_ptr_test2.m_weak_ptr.lock().get() == shared_ptr_test2.m_shared_ptr.get());
    assert(*shared_ptr_test2.m_int_ptr == 621);

    UniquePtrTest unique_ptr_test;
    unique_ptr_test.m_unique_ptr = std::make_unique<BaseData>();
    for (int i = 0; i < 3; i++) {
        unique_ptr_test.m_unique_ptr->data[i] = 182.376f * i;
    }
    archive.clear();
    Engine::Serialization::serialize(unique_ptr_test, archive);
    std::cout << "unique ptr test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    unique_ptr_test.m_unique_ptr.reset();
    UniquePtrTest unique_ptr_test2;
    Engine::Serialization::deserialize(unique_ptr_test2, archive);
    archive.clear();
    for (int i = 0; i < 3; i++) assert(unique_ptr_test2.m_unique_ptr->data[i] == 182.376f * i);

    VectorTest vector_test;
    for (int i = 0; i < 3; i++) {
        BaseData base_data;
        for (int j = 0; j < 3; j++) {
            base_data.data[j] = 100 * j + i * 3;
        }
        vector_test.m_vector.push_back(base_data);
    }
    archive.clear();
    Engine::Serialization::serialize(vector_test, archive);
    std::cout << "vector test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    vector_test.m_vector.clear();
    VectorTest vector_test2;
    Engine::Serialization::deserialize(vector_test2, archive);
    archive.clear();
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            assert(vector_test2.m_vector[i].data[j] == 100 * j + i * 3);
        }
    }

    CustomTest custom_test;
    custom_test.m_a = 123;
    custom_test.m_b = 852765;
    archive.clear();
    Engine::Serialization::serialize(custom_test, archive);
    std::cout << "custom test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    custom_test.m_a = 0;
    custom_test.m_b = 0;
    CustomTest custom_test2;
    Engine::Serialization::deserialize(custom_test2, archive);
    archive.clear();
    assert(custom_test2.m_a == 123);
    assert(custom_test2.m_b == 852765);

    PolymorphismTest polymorphism_test;
    auto base_data_ptr2 = std::make_shared<BaseData>();
    for (int i = 0; i < 3; i++) base_data_ptr2->data[i] = 100.0f * i;
    polymorphism_test.m_vector.push_back(base_data_ptr2);
    auto inherit_data_ptr = std::make_shared<InheritTest>();
    for (int i = 0; i < 3; i++) inherit_data_ptr->data[i] = 10.0f * i;
    inherit_data_ptr->m_inherit = 1000;
    polymorphism_test.m_vector.push_back(inherit_data_ptr);
    archive.clear();
    Engine::Serialization::serialize(polymorphism_test, archive);
    std::cout << "polymorphism test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    polymorphism_test.m_vector.clear();
    PolymorphismTest polymorphism_test2;
    Engine::Serialization::deserialize(polymorphism_test2, archive);
    archive.clear();
    assert(polymorphism_test2.m_vector.size() == 2);
    for (int i = 0; i < 3; i++) assert(polymorphism_test2.m_vector[0]->data[i] == 100.0f * i);
    auto inherit_data_ptr2 = std::dynamic_pointer_cast<InheritTest>(polymorphism_test2.m_vector[1]);
    assert(inherit_data_ptr2);
    for (int i = 0; i < 3; i++) assert(inherit_data_ptr2->data[i] == 10.0f * i);
    assert(inherit_data_ptr2->m_inherit == 1000);

    ArrayPtrTest array_ptr_test;
    for (int i = 0; i < 3; i++) array_ptr_test.m_array[i] = i * 100 + 1;
    base_data_ptr2 = std::make_shared<BaseData>();
    for (int i = 0; i < 3; i++) base_data_ptr2->data[i] = 100.0f * i;
    array_ptr_test.m_ptr_array[0][0] = base_data_ptr2;
    inherit_data_ptr = std::make_shared<InheritTest>();
    for (int i = 0; i < 3; i++) inherit_data_ptr->data[i] = 10.0f * i;
    inherit_data_ptr->m_inherit = 1000;
    array_ptr_test.m_ptr_array[1][1] = inherit_data_ptr;
    archive.clear();
    Engine::Serialization::serialize(array_ptr_test, archive);
    std::cout << "array ptr test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    ArrayPtrTest array_ptr_test2;
    Engine::Serialization::deserialize(array_ptr_test2, archive);
    archive.clear();
    for (int i = 0; i < 3; i++) assert(array_ptr_test2.m_array[i] == i * 100 + 1);
    for (int i = 0; i < 3; i++) assert(array_ptr_test2.m_ptr_array[0][0]->data[i] == 100.0f * i);
    auto inherit_data_ptr3 = dynamic_pointer_cast<InheritTest>(array_ptr_test2.m_ptr_array[1][1]);
    for (int i = 0; i < 3; i++) assert(inherit_data_ptr3->data[i] == 10.0f * i);
    assert(inherit_data_ptr3->m_inherit == 1000);
    assert(array_ptr_test2.m_ptr_array[0][1] == nullptr);
    assert(array_ptr_test2.m_ptr_array[1][0] == nullptr);

    std::shared_ptr<SelfPtrTest> self_ptr_test = std::make_shared<SelfPtrTest>();
    self_ptr_test->m_self_ptr = self_ptr_test;
    archive.clear();
    Engine::Serialization::serialize(self_ptr_test, archive);
    std::cout << "self ptr test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    self_ptr_test->m_self_ptr.reset();
    std::shared_ptr<SelfPtrTest> self_ptr_test2 = std::make_shared<SelfPtrTest>();
    Engine::Serialization::deserialize(self_ptr_test2, archive);
    archive.clear();
    assert(self_ptr_test2->m_self_ptr.get() == self_ptr_test2.get());

    StructStructTest struct_struct_test;
    struct_struct_test.m_inner_struct.m_inner_float = 3.1415926f;
    struct_struct_test.m_inner_struct.m_inner_int = 100;
    struct_struct_test.m_inner_class.m_inner_int = 200;
    struct_struct_test.m_double = 2.718281828459045;
    archive.clear();
    Engine::Serialization::serialize(struct_struct_test, archive);
    std::cout << "struct struct test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    struct_struct_test.m_inner_struct.m_inner_float = 0.0f;
    struct_struct_test.m_inner_struct.m_inner_int = 0;
    struct_struct_test.m_inner_class.m_inner_int = 0;
    struct_struct_test.m_double = 0.0;
    StructStructTest struct_struct_test2;
    Engine::Serialization::deserialize(struct_struct_test2, archive);
    archive.clear();
    assert(struct_struct_test2.m_inner_struct.m_inner_float == 3.1415926f);
    assert(struct_struct_test2.m_inner_struct.m_inner_int == 100);
    assert(struct_struct_test2.m_inner_class.m_inner_int == 200);
    assert(struct_struct_test2.m_double == 2.718281828459045);

    EnumTest enum_test;
    enum_test.m_normal_enum = EnumTest::TestNormalEnum::NE2;
    enum_test.m_color = EnumTest::Color::Green;
    archive.clear();
    Engine::Serialization::serialize(enum_test, archive);
    std::cout << "enum test:" << std::endl << archive.m_context->json.dump(4) << std::endl;
    enum_test.m_normal_enum = EnumTest::TestNormalEnum::NE3;
    enum_test.m_color = EnumTest::Color::Blue;
    Engine::Serialization::deserialize(enum_test, archive);
    archive.clear();
    assert(enum_test.m_normal_enum == EnumTest::TestNormalEnum::NE2);
    assert(enum_test.m_color == EnumTest::Color::Green);

    return 0;
}
