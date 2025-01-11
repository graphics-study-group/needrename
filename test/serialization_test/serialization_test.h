#ifndef CTEST_SERIALIZATION_TEST_H
#define CTEST_SERIALIZATION_TEST_H

#include <memory>
#include <vector>
#include <any>
#include <cstdint>
#include <meta_serialization_test/reflection.hpp>

namespace SerializationTest
{
    class REFL_SER_CLASS(REFL_BLACKLIST) StdintTest
    {
        REFL_SER_BODY(StdintTest)
    public:
        StdintTest() = default;
        virtual ~StdintTest() = default;

        int8_t m_int8 = 0;
        int16_t m_int16 = 0;
        int32_t m_int32 = 0;
        int64_t m_int64 = 0;
        uint8_t m_uint8 = 0;
        uint16_t m_uint16 = 0;
        uint32_t m_uint32 = 0;
        uint64_t m_uint64 = 0;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) StdAnyTest
    {
        REFL_SER_BODY(StdAnyTest)
    public:
        StdAnyTest() = default;
        virtual ~StdAnyTest() = default;

        std::vector<std::any> m_any_vector {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) BaseData
    {
        REFL_SER_BODY(BaseData)
    public:
        BaseData() = default;
        virtual ~BaseData() = default;

        float data[3] = {0.0f};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) InheritTest : public BaseData
    {
        REFL_SER_BODY(InheritTest)
    public:
        InheritTest() = default;
        virtual ~InheritTest() = default;

        int m_inherit = 1000;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) SharedPtrTest
    {
        REFL_SER_BODY(SharedPtrTest)
    public:
        SharedPtrTest() = default;
        virtual ~SharedPtrTest() = default;

        std::shared_ptr<BaseData> m_shared_ptr {};
        std::shared_ptr<BaseData> m_shared_ptr2 {};
        std::shared_ptr<int> m_int_ptr {};
        std::weak_ptr<BaseData> m_weak_ptr {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) UniquePtrTest
    {
        REFL_SER_BODY(UniquePtrTest)
    public:
        UniquePtrTest() = default;
        virtual ~UniquePtrTest() = default;

        std::unique_ptr<BaseData> m_unique_ptr {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) VectorTest
    {
        REFL_SER_BODY(VectorTest)
    public:
        VectorTest() = default;
        virtual ~VectorTest() = default;

        std::vector<BaseData> m_vector {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) PolymorphismTest
    {
        REFL_SER_BODY(PolymorphismTest)
    public:
        PolymorphismTest() = default;
        virtual ~PolymorphismTest() = default;

        std::vector<std::shared_ptr<BaseData>> m_vector {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) CustomTest
    {
        REFL_SER_BODY(CustomTest)
    public:
        CustomTest() = default;
        virtual ~CustomTest() = default;

        int m_a = 621;
        int m_b = 182376;

        REFL_SER_DISABLE inline void save_to_archive(Engine::Serialization::Archive& archive) const
        {
            Engine::Serialization::Json &json = *archive.m_cursor;
            json["data"] = m_a * 1000000 + m_b;
        }

        REFL_SER_DISABLE inline void load_from_archive(Engine::Serialization::Archive& archive)
        {
            Engine::Serialization::Json &json = *archive.m_cursor;
            m_a = json["data"].get<int>() / 1000000;
            m_b = json["data"].get<int>() % 1000000;
        }
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) ArrayPtrTest
    {
        REFL_SER_BODY(ArrayPtrTest)
    public:
        ArrayPtrTest() = default;
        virtual ~ArrayPtrTest() = default;

        int m_array[3] = {1, 2, 3};
        std::shared_ptr<BaseData> m_ptr_array[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) SelfPtrTest
    {
        REFL_SER_BODY(SelfPtrTest)
    public:
        SelfPtrTest() = default;
        virtual ~SelfPtrTest() = default;

        std::shared_ptr<SelfPtrTest> m_self_ptr {};
    };
}

#endif // CTEST_SERIALIZATION_TEST_H
