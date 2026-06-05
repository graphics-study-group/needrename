#ifndef CTEST_SERIALIZATION_NESTED_STRUCT_H
#define CTEST_SERIALIZATION_NESTED_STRUCT_H

#include <Reflection/macros.h>

namespace SerializationTest {
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
} // namespace SerializationTest

void RunSerializationNestedStructTest();

#endif // CTEST_SERIALIZATION_NESTED_STRUCT_H
