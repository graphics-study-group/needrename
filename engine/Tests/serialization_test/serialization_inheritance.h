#ifndef CTEST_SERIALIZATION_INHERITANCE_H
#define CTEST_SERIALIZATION_INHERITANCE_H

#include <Reflection/macros.h>

namespace SerializationTest {
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
} // namespace SerializationTest

void RunSerializationInheritanceTest();

#endif // CTEST_SERIALIZATION_INHERITANCE_H
