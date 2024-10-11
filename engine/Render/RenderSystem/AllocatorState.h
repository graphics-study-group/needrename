#ifndef RENDER_RENDERSYSTEM_VKALLOCATOR_INCLUDED
#define RENDER_RENDERSYSTEM_VKALLOCATOR_INCLUDED

#include <memory>
#include <vk_mem_alloc.h>

namespace Engine {
    class RenderSystem;
    namespace RenderSystemState {
        class AllocatorState {
            std::weak_ptr <RenderSystem> m_system {};
            VmaAllocator m_allocator {};
        public:
            AllocatorState() = default;
            AllocatorState(const AllocatorState &) = delete;
            void operator = (const AllocatorState &) = delete;

            ~AllocatorState();

            void Create(std::shared_ptr <RenderSystem> system);
            VmaAllocator GetAllocator() const;
        };
    }
}

#endif // RENDER_RENDERSYSTEM_VKALLOCATOR_INCLUDED
