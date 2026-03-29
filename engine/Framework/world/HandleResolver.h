#ifndef FRAMEWORK_WORLD_HANDLERESOLVER_INCLUDED
#define FRAMEWORK_WORLD_HANDLERESOLVER_INCLUDED

#include "Handle.h"
#include <Reflection/serialization.h>
#include <unordered_map>

namespace Engine {
    /**
     * @brief HandleResolver is a custom serialization resolver for Handle.
     * It is used for load one scene's GameObject to another scene.
     * It will map the ID of ObjectHandle to ComponentHandle from the original scene to the new scene.
     */
    class HandleResolver : public Engine::Serialization::Resolver {
    public:
        HandleResolver() = default;
        ~HandleResolver() = default;

        std::unordered_map<uint32_t, ObjectHandle> m_obj_map{};
        std::unordered_map<uint32_t, ComponentHandle> m_comp_map{};
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_HANDLERESOLVER_INCLUDED
