#ifndef CTEST_REFLECTION_LIFECYCLE_H
#define CTEST_REFLECTION_LIFECYCLE_H

#include <Reflection/macros.h>
#include <memory>

class REFL_SER_CLASS(REFL_WHITELIST) LifecycleTest {
    REFL_SER_BODY(LifecycleTest)
public:
    struct InnerProbe {
        static inline int alive = 0;
        static inline int destroyed = 0;
        InnerProbe() {
            ++alive;
        }
        ~InnerProbe() {
            ++destroyed;
            --alive;
        }
    };

    REFL_ENABLE LifecycleTest() : m_probe(std::make_shared<InnerProbe>()) {
        ++constructed;
        ++alive;
    }
    virtual ~LifecycleTest() {
        ++destructed;
        --alive;
    }

    REFL_ENABLE LifecycleTest MakeAnother() const {
        return LifecycleTest();
    }

    std::shared_ptr<InnerProbe> m_probe{};

    static inline int constructed = 0;
    static inline int destructed = 0;
    static inline int alive = 0;

    static void ResetCounters() {
        constructed = 0;
        destructed = 0;
        alive = 0;
        InnerProbe::destroyed = 0;
        InnerProbe::alive = 0;
    }
};

void RunReflectionLifecycleTest();

#endif // CTEST_REFLECTION_LIFECYCLE_H
