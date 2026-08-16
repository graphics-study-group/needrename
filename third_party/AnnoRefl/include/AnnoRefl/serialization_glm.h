#ifndef ANROREFL_SERIALIZATION_GLM_INCLUDED
#define ANROREFL_SERIALIZATION_GLM_INCLUDED

#include "Export.h"
#include "serialization.h"
#include <glm.hpp>
#include <gtc/quaternion.hpp>

namespace AnnoRefl {
    ANROREFL_API void save_to_archive(const glm::vec2 &value, Archive &archive);
    ANROREFL_API void load_from_archive(glm::vec2 &value, Archive &archive);

    ANROREFL_API void save_to_archive(const glm::vec3 &value, Archive &archive);
    ANROREFL_API void load_from_archive(glm::vec3 &value, Archive &archive);

    ANROREFL_API void save_to_archive(const glm::vec4 &value, Archive &archive);
    ANROREFL_API void load_from_archive(glm::vec4 &value, Archive &archive);

    ANROREFL_API void save_to_archive(const glm::quat &value, Archive &archive);
    ANROREFL_API void load_from_archive(glm::quat &value, Archive &archive);

    ANROREFL_API void save_to_archive(const glm::mat3 &value, Archive &archive);
    ANROREFL_API void load_from_archive(glm::mat3 &value, Archive &archive);

    ANROREFL_API void save_to_archive(const glm::mat4 &value, Archive &archive);
    ANROREFL_API void load_from_archive(glm::mat4 &value, Archive &archive);
} // namespace AnnoRefl

#endif // ANROREFL_SERIALIZATION_GLM_INCLUDED
