#ifndef CTEST_SERIALIZATION_TEST_H
#define CTEST_SERIALIZATION_TEST_H

#include <Reflection/enum_factory.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_any.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <any>
#include <cstdint>
#include <memory>
#include <vector>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) StdintTest {
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

    class REFL_SER_CLASS(REFL_BLACKLIST) StdAnyTest {
        REFL_SER_BODY(StdAnyTest)
    public:
        StdAnyTest() = default;
        virtual ~StdAnyTest() = default;

        std::vector<std::any> m_any_vector{};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) BaseData {
        REFL_SER_BODY(BaseData)
    public:
        BaseData() = default;
        virtual ~BaseData() = default;

        float data[3] = {0.0f};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) InheritTest : public BaseData {
        REFL_SER_BODY(InheritTest)
    public:
        InheritTest() = default;
        virtual ~InheritTest() = default;

        int m_inherit = 1000;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) SharedPtrTest {
        REFL_SER_BODY(SharedPtrTest)
    public:
        SharedPtrTest() = default;
        virtual ~SharedPtrTest() = default;

        std::shared_ptr<BaseData> m_shared_ptr{};
        std::shared_ptr<BaseData> m_shared_ptr2{};
        std::shared_ptr<int> m_int_ptr{};
        std::weak_ptr<BaseData> m_weak_ptr{};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) UniquePtrTest {
        REFL_SER_BODY(UniquePtrTest)
    public:
        UniquePtrTest() = default;
        virtual ~UniquePtrTest() = default;

        std::unique_ptr<BaseData> m_unique_ptr{};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) VectorTest {
        REFL_SER_BODY(VectorTest)
    public:
        VectorTest() = default;
        virtual ~VectorTest() = default;

        std::vector<BaseData> m_vector{};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) PolymorphismTest {
        REFL_SER_BODY(PolymorphismTest)
    public:
        PolymorphismTest() = default;
        virtual ~PolymorphismTest() = default;

        std::vector<std::shared_ptr<BaseData>> m_vector{};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) CustomTest {
        REFL_SER_BODY(CustomTest)
    public:
        CustomTest() = default;
        virtual ~CustomTest() = default;

        int m_a = 621;
        int m_b = 182376;

        REFL_SER_DISABLE inline void save_to_archive(Engine::Serialization::Archive &archive) const {
            Engine::Serialization::Json &json = *archive.m_cursor;
            json["data"] = m_a * 1000000 + m_b;
        }

        REFL_SER_DISABLE inline void load_from_archive(Engine::Serialization::Archive &archive) {
            Engine::Serialization::Json &json = *archive.m_cursor;
            m_a = json["data"].get<int>() / 1000000;
            m_b = json["data"].get<int>() % 1000000;
        }
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) ArrayPtrTest {
        REFL_SER_BODY(ArrayPtrTest)
    public:
        ArrayPtrTest() = default;
        virtual ~ArrayPtrTest() = default;

        int m_array[3] = {1, 2, 3};
        std::shared_ptr<BaseData> m_ptr_array[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) SelfPtrTest {
        REFL_SER_BODY(SelfPtrTest)
    public:
        SelfPtrTest() = default;
        virtual ~SelfPtrTest() = default;

        std::shared_ptr<SelfPtrTest> m_self_ptr{};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) StructStructTest {
        REFL_SER_BODY(StructStructTest)
    public:
        StructStructTest() = default;
        virtual ~StructStructTest() = default;

        class REFL_SER_CLASS(REFL_BLACKLIST) InnerClass;

        struct REFL_SER_CLASS(REFL_BLACKLIST) InnerStruct {
            REFL_SER_SIMPLE_STRUCT(InnerStruct)

            float m_inner_float = 0.0f;
            int m_inner_int = 0;
        } m_inner_struct{};

        class REFL_SER_CLASS(REFL_BLACKLIST) InnerClass {
            REFL_SER_BODY(InnerClass)
        public:
            InnerClass() = default;
            virtual ~InnerClass() = default;

            int m_inner_int = 0;
        };

        InnerClass m_inner_class{};
        double m_double = 0.0;
    };

    #define TestMagicEnumDefine(XMACRO, enum_name) \
    XMACRO(enum_name, E1) \
    XMACRO(enum_name, E2) \
    XMACRO(enum_name, E3)

    class REFL_SER_CLASS(REFL_WHITELIST) EnumTest {
        REFL_SER_BODY(EnumTest)
    public:
        EnumTest() = default;
        virtual ~EnumTest() = default;

        DECLARE_ENUM(TestMagicEnum, TestMagicEnumDefine)
        REFL_SER_ENABLE TestMagicEnum m_magic_enum = TestMagicEnum::E1;
        REFL_SER_ENABLE enum class TestNormalEnum { NE1, NE2, NE3 } m_normal_enum = TestNormalEnum::NE1;

        enum class REFL_SER_CLASS() Color {
            Red,
            Green,
            Blue
        };
        REFL_SER_ENABLE Color m_color = Color::Red;
    };
} // namespace SerializationTest

DECLARE_REFLECTIVE_FUNCTIONS(SerializationTest::EnumTest::TestMagicEnum, TestMagicEnumDefine)

#endif // CTEST_SERIALIZATION_TEST_H
