#ifndef ENGINE_RHI_COMPUTEKERNEL_INCLUDED
#define ENGINE_RHI_COMPUTEKERNEL_INCLUDED

#include "Rhi/Texture/TextureSubresourceView.h"
#include "Rhi/rhi_export.h"

#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    class DeviceBuffer;
    class DeviceContext;
    class Texture;

    /**
     * @brief One resource a caller binds to a declared shader interface.
     *
     * An entry is either a buffer — optionally restricted to an offset and size
     * — or a texture, optionally restricted to a subresource range. `kind`
     * selects the populated arm.
     */
    struct RHI_API ComputeKernelResource {
        enum class Kind : uint8_t {
            Buffer,
            Texture,
        };

        struct BufferEntry {
            vk::Buffer handle{};
            size_t offset{0};
            size_t size{std::numeric_limits<size_t>::max()};
        };
        struct ImageEntry {
            vk::ImageView view{};
            vk::Sampler sampler{};
        };

        Kind kind{Kind::Buffer};
        union {
            BufferEntry buffer;
            ImageEntry image;
        };

        /// @brief The union carries no initializer of its own.
        ComputeKernelResource() : buffer{} {
        }

        /**
         * @brief Bind all of a buffer.
         *
         * The implicit form exists so a dictionary entry reads as the buffer it
         * binds — `{"KeysIn", keys_buffer}` — and no binding site has to restate
         * the bound extent. Use `Buffer()` to restrict the range.
         */
        ComputeKernelResource(const DeviceBuffer &buffer);

        /**
         * @brief Bind a buffer, optionally restricted to a sub-range.
         *
         * @param buffer The buffer to bind.
         * @param offset Byte offset the bound range starts at.
         * @param size Byte size of the bound range; the default binds all of it.
         */
        static ComputeKernelResource Buffer(
            const DeviceBuffer &buffer, size_t offset = 0, size_t size = std::numeric_limits<size_t>::max()
        );

        /**
         * @brief Bind an image interface to a texture, optionally restricted to
         * a subresource range.
         */
        static ComputeKernelResource Image(
            Texture &texture, const TextureSubresourceRange &range = TextureSubresourceRange::GetFullRange()
        );
    };

    /**
     * @brief The name-to-resource dictionary a dispatch is invoked with.
     */
    using ComputeResourceDictionary = std::initializer_list<std::pair<std::string_view, ComputeKernelResource>>;

    /**
     * @brief A compute shader's pipeline, invoked like a function.
     *
     * A kernel is created from a SPIR-V module together with a module identity,
     * and is requested from the device context, which holds exactly one kernel
     * per identity. Its pipeline, pipeline layout, descriptor-set layout and
     * resolved interface table are immutable after creation.
     *
     * Invoking it is a single `Dispatch` call taking the command buffer, the
     * resources keyed by the shader's declared interface names, the workgroup
     * counts for all three dimensions, and the push-constant value. Interface
     * names are resolved against the reflected interface table; no descriptor
     * set or binding number is stated on the C++ side, and a dictionary that
     * mismatches the shader's declarations throws `std::runtime_error`.
     *
     * A dispatch acquires its descriptor set from the device descriptor arena
     * on every call and retains no set handle between calls. It records no
     * barrier and takes no rotation, slot or frame parameter: synchronization
     * between dispatches is the caller's responsibility.
     *
     * The kernel binds descriptor set 0 only, and it depends on no Asset type.
     */
    class RHI_API ComputeKernel {
    public:
        ~ComputeKernel();

        ComputeKernel(const ComputeKernel &) = delete;
        ComputeKernel &operator=(const ComputeKernel &) = delete;

        /// @brief Get the name this kernel was created with.
        const std::string &GetName() const noexcept;
        /// @brief Get the reflected size in bytes of the push-constant block (0 if none).
        uint32_t GetPushConstantSize() const noexcept;
        /// @brief Get the compute pipeline.
        vk::Pipeline GetPipeline() const noexcept;
        /// @brief Get the pipeline layout.
        vk::PipelineLayout GetPipelineLayout() const noexcept;
        /// @brief Get the descriptor set layout the arena resolved for this kernel.
        vk::DescriptorSetLayout GetDescriptorSetLayout() const noexcept;

        /**
         * @brief Dispatch this kernel with no push-constant value.
         *
         * For a shader that declares no push-constant block.
         */
        void Dispatch(
            vk::CommandBuffer cb,
            ComputeResourceDictionary resources,
            uint32_t group_count_x,
            uint32_t group_count_y,
            uint32_t group_count_z
        ) const;

        /**
         * @brief Dispatch this kernel, recording `sizeof(T)` bytes of `push` at offset 0.
         *
         * Debug builds assert that the value fits the reflected block.
         */
        template <typename T>
        void Dispatch(
            vk::CommandBuffer cb,
            ComputeResourceDictionary resources,
            uint32_t group_count_x,
            uint32_t group_count_y,
            uint32_t group_count_z,
            const T &push
        ) const {
            static_assert(sizeof(T) % 4 == 0, "Push constant size must be a multiple of 4 bytes.");
            assert(sizeof(T) <= GetPushConstantSize() && "Push constant value exceeds the reflected block size.");
            DispatchAndRecord(cb, resources, group_count_x, group_count_y, group_count_z, &push, sizeof(T));
        }

    private:
        friend class ComputeKernelCache;

        explicit ComputeKernel(DeviceContext &device_context);

        /// @brief Reflect the module and build the pipeline, layouts and table.
        void Create(const std::vector<uint32_t> &spirv_code, std::string_view name);

        /// @brief The dispatcher both public overloads funnel into.
        void DispatchAndRecord(
            vk::CommandBuffer cb,
            ComputeResourceDictionary resources,
            uint32_t group_count_x,
            uint32_t group_count_y,
            uint32_t group_count_z,
            const void *push,
            size_t push_size
        ) const;

        struct impl;
        std::unique_ptr<impl> pimpl;
    };

    /**
     * @brief Device-scoped cache of compute kernels, keyed by module identity.
     *
     * Requesting a kernel for an identity the device already knows yields the
     * kernel created for it: its pipeline, pipeline layout and descriptor-set
     * layout exist exactly once per identity, however many components dispatch
     * it. Creation is lazy — nothing is created for an identity before its
     * first request — and the cache is bounded by construction, because the
     * number of distinct modules is fixed at build time.
     *
     * The identity is the caller's assertion that two requests bearing it
     * denote the same module. It is never compared against the module's words,
     * and a request that hits ignores the words it is handed. Identities are
     * expected to come from wherever a shader is already managed — a shader
     * asset's GUID, or the path a module was loaded from — rather than to be
     * chosen at a dispatch site.
     */
    class RHI_API ComputeKernelCache {
    public:
        explicit ComputeKernelCache(DeviceContext &device_context);
        ~ComputeKernelCache();

        ComputeKernelCache(const ComputeKernelCache &) = delete;
        ComputeKernelCache &operator=(const ComputeKernelCache &) = delete;

        /**
         * @brief Find the kernel created for a module identity.
         *
         * @param module_id Identity to probe.
         * @return The kernel, or nullptr when none exists for it. Nothing is
         * created, and no module is read or scanned.
         */
        ComputeKernel *Find(std::string_view module_id) noexcept;

        /**
         * @brief Get the kernel for a module identity, creating it on a miss.
         *
         * @param module_id Identity of the module, unique per module on this device.
         * @param spirv_code The module's SPIR-V words; used only when the
         * identity is new.
         * @param debug_name Debug name applied to the kernel and its objects;
         * the identity is used when it is empty.
         */
        ComputeKernel &Request(
            std::string_view module_id, const std::vector<uint32_t> &spirv_code, std::string_view debug_name = {}
        );

        /// @brief Get the number of distinct kernels the cache holds.
        size_t GetKernelCount() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_COMPUTEKERNEL_INCLUDED
