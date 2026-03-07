#ifndef FRAMEWORK_WORLD_HANDLERESOLVER_INCLUDED
#define FRAMEWORK_WORLD_HANDLERESOLVER_INCLUDED

#include <Reflection/serialization.h>
#include <unordered_map>
#include "Handle.h"

namespace Engine {
    class HandleResolver : public Engine::Serialization::Resolver {
    public:
        HandleResolver() = default;
        ~HandleResolver() = default;

        std::unordered_map<uint32_t, ObjectHandle> m_obj_map{};
        std::unordered_map<uint32_t, ComponentHandle> m_comp_map{};
    };
} // namespace Engine

#endif // FRAMEWORK_WORLD_HANDLERESOLVER_INCLUDED
