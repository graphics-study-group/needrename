#ifndef RENDER_RESOURCE_IASYCHPREPARED_INCLUDED
#define RENDER_RESOURCE_IASYCHPREPARED_INCLUDED

namespace Engine {
    namespace RenderSystemState {
        class SubmissionHelper;
        class AllocatorState;
    } // namespace RenderSystemState

    /**
     * @brief Interface for GPU resources that have an explicit preparation/removal lifecycle.
     *
     * @details
     * A resource implementing this interface is managed by a render-resource manager.
     * The manager owns lifetime bookkeeping (handle/refcount/deferred destroy), while the
     * resource implements backend-specific work:
     * - `Submit`: prepare GPU-visible data (sync or async scheduling model defined by caller).
     * - `IsReady`: report whether the resource is currently usable by render passes.
     * - `Remove`: release GPU-side allocations and reset internal prepared state.
     */
    class IAsynchPrepared {
    public:
        IAsynchPrepared() noexcept = default;
        virtual ~IAsynchPrepared() noexcept = default;

        /**
         * @brief Query whether the resource can be used directly by rendering code.
         * @return True when resource is ready; otherwise false.
         */
        virtual bool IsReady() const noexcept = 0;

        /**
         * @brief Release prepared GPU-side allocations and mark resource as not ready.
         */
        virtual void Remove() noexcept = 0;

        /**
         * @brief Submit/prepare resource data for GPU usage.
         *
         * @param allocator Allocator state used to create GPU allocations.
         * @param submission_helper Submission helper used to enqueue copy/upload commands.
         *
         * @note Implementation may perform eager upload or only enqueue async operations, depending on resource/manager policy.
         */
        virtual void Submit(
            const RenderSystemState::AllocatorState &allocator, RenderSystemState::SubmissionHelper &submission_helper
        ) = 0;
    };
} // namespace Engine

#endif // RENDER_RESOURCE_IASYCHPREPARED_INCLUDED
