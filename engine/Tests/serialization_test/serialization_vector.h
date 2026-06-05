#ifndef CTEST_SERIALIZATION_VECTOR_H
#define CTEST_SERIALIZATION_VECTOR_H

#include <Reflection/macros.h>
#include <Reflection/serialization_vector.h>
#include <vector>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) BaseData {
        REFL_SER_BODY(BaseData)
    public:
        BaseData() = default;
        virtual ~BaseData() = default;

        float data[3] = {0.0f};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) VectorTest {
        REFL_SER_BODY(VectorTest)
    public:
        VectorTest() = default;
        virtual ~VectorTest() = default;

        std::vector<BaseData> m_vector{};
    };
} // namespace SerializationTest

void RunSerializationVectorTest();

#endif // CTEST_SERIALIZATION_VECTOR_H
