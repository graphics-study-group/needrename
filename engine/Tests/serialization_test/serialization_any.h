#ifndef CTEST_SERIALIZATION_ANY_H
#define CTEST_SERIALIZATION_ANY_H

#include <Reflection/macros.h>
#include <Reflection/serialization_any.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <any>
#include <vector>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) StdAnyTest {
        REFL_SER_BODY(StdAnyTest)
    public:
        StdAnyTest() = default;
        virtual ~StdAnyTest() = default;

        std::vector<std::any> m_any_vector{};
    };
} // namespace SerializationTest

void RunSerializationAnyTest();

#endif // CTEST_SERIALIZATION_ANY_H
