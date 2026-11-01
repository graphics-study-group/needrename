#ifndef ENGINE_RHI_PIPELINE_OBJECT_INCLUDED
#define ENGINE_RHI_PIPELINE_OBJECT_INCLUDED

#include "Rhi/Pipeline/PipelineBuilder.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>

namespace Engine::Rhi {

    class DeviceContext;

    /**
     * @brief A pipeline built for one set of state, and its lifetime.
     *
     * Building a pipeline is when a backend's state vocabulary lands: the
     * description names state groups, vertex input, attachment formats and the
     * shaders with their stages, and the backend turns that into whatever its API
     * wants. The pipeline owns itself, so a material that holds one does not have
     * to pair a build with a destroy.
     *
     * @note Movable but non-copyable.
     */
    class RHI_API Pipeline {
    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

        Pipeline(std::unique_ptr<impl> p) noexcept;

    public:
        /**
         * @brief Build a graphics pipeline.
         *
         * @param device_context The device that owns it.
         * @param description The shaders, the layout and the state.
         * @return The pipeline. A failed build yields a pipeline whose handle is
         * invalid rather than throwing, because a material that cannot build is a
         * content problem, not a programming one.
         */
        static Pipeline Create(const DeviceContext &device_context, const GraphicsPipelineBuildDescription &description);

        ~Pipeline();

        Pipeline(const Pipeline &) = delete;
        Pipeline &operator=(const Pipeline &) = delete;
        Pipeline(Pipeline &&) noexcept;
        Pipeline &operator=(Pipeline &&) noexcept;

        /// @brief The portable handle, for a bind call.
        PipelineHandle GetHandle() const noexcept;

        /// @brief Whether the build succeeded.
        bool IsValid() const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_PIPELINE_OBJECT_INCLUDED
