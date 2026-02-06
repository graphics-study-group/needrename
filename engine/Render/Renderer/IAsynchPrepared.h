#ifndef RENDER_RENDERER_IASYCHPREPARED_INCLUDED
#define RENDER_RENDERER_IASYCHPREPARED_INCLUDED

namespace Engine {
    namespace RenderSystemState {
        class SubmissionHelper;
        class AllocatorState;
    }
    /**
     * @brief A render resource that needs to be submitted to GPU asynchronously.
     */
    class IAsynchPrepared {
    public:
        IAsynchPrepared() noexcept = default;
        virtual ~IAsynchPrepared() noexcept = default;

        /**
         * @brief Is this resource prepared, or scheduled to be prepared, so
         * that it can be used directly?
         */
        virtual bool IsReady() const noexcept = 0;

        /**
         * @brief Flag this resource to be removed from GPU.
         */
        virtual void Remove() noexcept = 0;

        /**
         * @brief How to submit this resource to GPU.
         */
        virtual void Submit(
            const RenderSystemState::AllocatorState &,
            RenderSystemState::SubmissionHelper &
        ) = 0;
    };
}

#endif // RENDER_RENDERER_IASYCHPREPARED_INCLUDED
