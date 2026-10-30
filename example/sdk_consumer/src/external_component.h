#ifndef SDK_CONSUMER_EXTERNAL_COMPONENT_H
#define SDK_CONSUMER_EXTERNAL_COMPONENT_H

#include <AnnoRefl/macros.h>
#include <AnnoRefl/serialization_vector.h>
#include <Core/Math/Transform.h>

#include <vector>

// A consumer type deriving from an engine type. The parser learns
// Engine::Transform from the SDK's shipped type lists, so it never rescans the
// engine headers to make the base type known.
class REFL_SER_CLASS(REFL_BLACKLIST) ExternalSpinner : public Engine::Transform {
    REFL_SER_BODY_OVERRIDE(ExternalSpinner)
public:
    ExternalSpinner() = default;

    float m_speed = 10.0f;
    std::vector<float> m_samples{1.0f, 2.0f, 3.0f};
};

#endif // SDK_CONSUMER_EXTERNAL_COMPONENT_H
