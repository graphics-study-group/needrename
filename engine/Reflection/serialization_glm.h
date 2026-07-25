#ifndef ENGINE_REFLECTION_SERIALIZATION_GLM_INCLUDED
#define ENGINE_REFLECTION_SERIALIZATION_GLM_INCLUDED

#include "reflection_export.h"
#include "serialization.h"
#include <glm.hpp>
#include <gtc/quaternion.hpp>

namespace Engine {
    namespace Serialization {
        REFLECTION_API void save_to_archive(const glm::vec2 &value, Archive &archive);
        REFLECTION_API void load_from_archive(glm::vec2 &value, Archive &archive);

        REFLECTION_API void save_to_archive(const glm::vec3 &value, Archive &archive);
        REFLECTION_API void load_from_archive(glm::vec3 &value, Archive &archive);

        REFLECTION_API void save_to_archive(const glm::vec4 &value, Archive &archive);
        REFLECTION_API void load_from_archive(glm::vec4 &value, Archive &archive);

        REFLECTION_API void save_to_archive(const glm::quat &value, Archive &archive);
        REFLECTION_API void load_from_archive(glm::quat &value, Archive &archive);

        REFLECTION_API void save_to_archive(const glm::mat3 &value, Archive &archive);
        REFLECTION_API void load_from_archive(glm::mat3 &value, Archive &archive);

        REFLECTION_API void save_to_archive(const glm::mat4 &value, Archive &archive);
        REFLECTION_API void load_from_archive(glm::mat4 &value, Archive &archive);
    } // namespace Serialization
} // namespace Engine

#endif // ENGINE_REFLECTION_SERIALIZATION_GLM_INCLUDED
