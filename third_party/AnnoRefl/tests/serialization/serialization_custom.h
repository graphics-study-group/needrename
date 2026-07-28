#ifndef CTEST_SERIALIZATION_CUSTOM_H
#define CTEST_SERIALIZATION_CUSTOM_H

#include <AnnoRefl/macros.h>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) CustomTest {
        REFL_SER_BODY(CustomTest)
    public:
        CustomTest() = default;
        virtual ~CustomTest() = default;

        int m_a = 621;
        int m_b = 182376;

        REFL_SER_DISABLE void save_to_archive(AnnoRefl::Archive &archive) const;
        REFL_SER_DISABLE void load_from_archive(AnnoRefl::Archive &archive);
    };
} // namespace SerializationTest

void RunSerializationCustomTest();

#endif // CTEST_SERIALIZATION_CUSTOM_H
