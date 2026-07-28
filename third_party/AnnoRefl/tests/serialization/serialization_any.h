#ifndef CTEST_SERIALIZATION_ANY_H
#define CTEST_SERIALIZATION_ANY_H

#include <AnnoRefl/macros.h>
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
