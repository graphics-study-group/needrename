#ifndef CTEST_SERIALIZATION_CUSTOM_H
#define CTEST_SERIALIZATION_CUSTOM_H

#include <Reflection/macros.h>
#include <Reflection/serialization.h>

namespace SerializationTest {
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
} // namespace SerializationTest

void RunSerializationCustomTest();

#endif // CTEST_SERIALIZATION_CUSTOM_H
