#ifndef REFLECTION_SERIALIZATION_GLM_INCLUDED
#define REFLECTION_SERIALIZATION_GLM_INCLUDED

#include <glm.hpp>
#include <gtc/quaternion.hpp>
#include "serialization.h"

namespace Engine
{
    namespace Serialization
    {
        void save_to_archive(const glm::vec2 &value, Archive &archive);
        void load_from_archive(glm::vec2 &value, Archive &archive);

        void save_to_archive(const glm::vec3 &value, Archive &archive);
        void load_from_archive(glm::vec3 &value, Archive &archive);

        void save_to_archive(const glm::vec4 &value, Archive &archive);
        void load_from_archive(glm::vec4 &value, Archive &archive);

        void save_to_archive(const glm::quat &value, Archive &archive);
        void load_from_archive(glm::quat &value, Archive &archive);

        void save_to_archive(const glm::mat3 &value, Archive &archive);
        void load_from_archive(glm::mat3 &value, Archive &archive);

        void save_to_archive(const glm::mat4 &value, Archive &archive);
        void load_from_archive(glm::mat4 &value, Archive &archive);
    }
}

#endif // REFLECTION_SERIALIZATION_GLM_INCLUDED
