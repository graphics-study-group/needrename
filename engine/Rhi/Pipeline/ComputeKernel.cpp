#include "Rhi/Pipeline/ComputeKernel.h"

#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/DebugUtils.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Pipeline/InterfaceBindingResolver.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Texture/Texture.h"

#include <algorithm>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Engine::Rhi {
    namespace {
        /// @brief A stable display name for a kernel created without a debug name.
        constexpr std::string_view UNNAMED_KERNEL = "<unnamed>";

        /// @brief Hashes a module identity without materializing a `std::string`.
        struct StringHash {
            using is_transparent = void;

            size_t operator()(std::string_view value) const noexcept {
                return std::hash<std::string_view>{}(value);
            }
        };
    } // namespace

    struct ComputePassInfo {
        vk::UniquePipeline pipeline{};
        vk::UniquePipelineLayout pipeline_layout{};
        /// @note The descriptor set layout is owned by the immutable
        /// resource cache, never by the pass.
        vk::DescriptorSetLayout desc_layout{};
        vk::UniqueShaderModule shader;
    };

    /// @brief One declared interface, resolved once at kernel creation.
    struct ComputeKernel::impl {
        struct Interface {
            std::string name{};
            uint32_t binding{0};
            /// @brief Descriptor type the interface is declared with.
            vk::DescriptorType type{vk::DescriptorType::eUniformBuffer};
            /// @brief Layout an image interface is bound in.
            vk::ImageLayout image_layout{vk::ImageLayout::eReadOnlyOptimal};
            /// @brief Whether the interface is an image rather than a buffer.
            bool is_image{false};
        };

        DeviceContext *device_context{nullptr};
        std::string name{};
        ComputePassInfo pass_info{};
        uint32_t push_constant_size{0};

        /// @brief Declared interfaces of set 0, ascending by binding number.
        std::vector<Interface> interfaces{};
        /// @brief Name index into `interfaces`.
        std::unordered_map<std::string_view, uint32_t> interface_by_name{};

        std::string DisplayName() const {
            return name.empty() ? std::string{UNNAMED_KERNEL} : name;
        }

        void Create(DeviceContext &context, const std::vector<uint32_t> &spirv_code, std::string_view requested_name) {
            device_context = &context;
            name = std::string{requested_name};

            const auto &device_interface = context.GetDeviceInterface();
            vk::Device device = device_interface.GetDevice();

            // Reflect the module and resolve the set layout once, through the
            // arena, so the layout the pipeline layout is built over is the
            // layout a set is allocated against. Uniform buffers are bound
            // statically: the kernel rotates nothing and binds no dynamic
            // offsets.
            auto layout = Rhi::SPLayout::Reflect(spirv_code, false);
            auto desc_bindings = layout.GenerateLayoutBindings(0, false, false);
            vk::DescriptorSetLayoutCreateInfo dslci{vk::DescriptorSetLayoutCreateFlags{}, desc_bindings};
            pass_info.desc_layout = context.GetDescriptorArena().ResolveLayout(
                dslci, std::format("Descriptor Set Layout - Compute Kernel {}", DisplayName()).c_str()
            );

            push_constant_size = layout.push_constant_size;
            std::vector<vk::PushConstantRange> pc_ranges;
            if (push_constant_size > 0) {
                pc_ranges.emplace_back(vk::ShaderStageFlagBits::eCompute, 0, push_constant_size);
            }
            vk::PipelineLayoutCreateInfo plci{vk::PipelineLayoutCreateFlags{}, {pass_info.desc_layout}, pc_ranges};
            pass_info.pipeline_layout = device.createPipelineLayoutUnique(plci);
            DEBUG_SET_NAME_TEMPLATE(
                device,
                pass_info.pipeline_layout.get(),
                std::format("Pipeline Layout for Compute Kernel {}", DisplayName())
            );

            vk::ShaderModuleCreateInfo smci{
                vk::ShaderModuleCreateFlags{},
                spirv_code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *>(spirv_code.data())
            };
            pass_info.shader = device.createShaderModuleUnique(smci);
            DEBUG_SET_NAME_TEMPLATE(
                device, pass_info.shader.get(), std::format("Shader Module for Compute Kernel {}", DisplayName())
            );

            vk::PipelineShaderStageCreateInfo pssci{
                vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eCompute, pass_info.shader.get(), "main"
            };
            vk::ComputePipelineCreateInfo cpci{vk::PipelineCreateFlags{}, pssci, pass_info.pipeline_layout.get()};
            auto ret = device.createComputePipelineUnique(nullptr, cpci);
            pass_info.pipeline = std::move(ret.value);
            DEBUG_SET_NAME_TEMPLATE(device, pass_info.pipeline.get(), std::format("Compute Kernel {}", DisplayName()));

            // Resolve the declared interfaces of set 0 into a dense table.
            // `SPLayout::interfaces` is sorted by set and binding, so filtering
            // set 0 keeps the table ascending by binding number. The reserve is
            // an upper bound: it is what lets the index below hold views into
            // the table's own strings instead of a second copy of every name.
            interfaces.clear();
            interface_by_name.clear();
            interfaces.reserve(layout.interfaces.size());
            for (const auto &pinterface : layout.interfaces) {
                if (pinterface->layout_set != 0) continue;

                const auto resolved = ResolveInterfaceBinding(*pinterface, false, false);
                assert(resolved && "Compute kernel encountered an interface kind it cannot bind.");
                if (!resolved) continue;

                interfaces.push_back(
                    Interface{
                        .name = pinterface->name,
                        .binding = pinterface->layout_binding,
                        .type = resolved->type,
                        .image_layout = resolved->image_layout,
                        .is_image = resolved->is_image
                    }
                );
            }

            // Index the table by the names it holds, once it has stopped growing.
            interface_by_name.reserve(interfaces.size());
            for (uint32_t index = 0; index < interfaces.size(); index++) {
                interface_by_name.emplace(interfaces[index].name, index);
            }
        }
    };

    ComputeKernelResource ComputeKernelResource::Buffer(const DeviceBuffer &buffer, size_t offset, size_t size) {
        ComputeKernelResource resource;
        resource.kind = Kind::Buffer;
        resource.buffer.handle = buffer.GetBuffer();
        resource.buffer.offset = offset;
        resource.buffer.size = size;
        return resource;
    }

    ComputeKernelResource ComputeKernelResource::Image(Texture &texture, const TextureSubresourceRange &range) {
        ComputeKernelResource resource;
        resource.kind = Kind::Texture;
        resource.image.view = texture.GetImageView(range);
        resource.image.sampler = texture.GetSampler();
        return resource;
    }

    ComputeKernel::ComputeKernel(DeviceContext &device_context) : pimpl(std::make_unique<impl>()) {
        (void)device_context;
    }

    ComputeKernel::~ComputeKernel() = default;

    const std::string &ComputeKernel::GetName() const noexcept {
        return pimpl->name;
    }

    uint32_t ComputeKernel::GetPushConstantSize() const noexcept {
        return pimpl->push_constant_size;
    }

    vk::Pipeline ComputeKernel::GetPipeline() const noexcept {
        return pimpl->pass_info.pipeline.get();
    }

    vk::PipelineLayout ComputeKernel::GetPipelineLayout() const noexcept {
        return pimpl->pass_info.pipeline_layout.get();
    }

    vk::DescriptorSetLayout ComputeKernel::GetDescriptorSetLayout() const noexcept {
        return pimpl->pass_info.desc_layout;
    }

    void ComputeKernel::Dispatch(
        vk::CommandBuffer cb,
        ComputeResourceDictionary resources,
        uint32_t group_count_x,
        uint32_t group_count_y,
        uint32_t group_count_z
    ) const {
        assert(
            pimpl->push_constant_size == 0 && "This kernel declares a push-constant block; dispatch it with a value."
        );
        DispatchAndRecord(cb, resources, group_count_x, group_count_y, group_count_z, nullptr, 0);
    }

    void ComputeKernel::DispatchAndRecord(
        vk::CommandBuffer cb,
        ComputeResourceDictionary resources,
        uint32_t group_count_x,
        uint32_t group_count_y,
        uint32_t group_count_z,
        const void *push,
        size_t push_size
    ) const {
        // Match every supplied name against the resolved table once, then order
        // the matched interfaces by binding number: the arena's content key is
        // order-sensitive, so the caller's dictionary order must not leak into
        // the entries it is handed.
        struct Matched {
            uint32_t binding;
            uint32_t index;
            const ComputeKernelResource *resource;
        };
        std::vector<Matched> matched;
        matched.reserve(resources.size());

        for (const auto &[key, resource] : resources) {
            auto itr = pimpl->interface_by_name.find(key);
            if (itr == pimpl->interface_by_name.end()) {
                throw std::runtime_error(
                    std::format("Compute kernel '{}' does not declare interface '{}'.", pimpl->DisplayName(), key)
                );
            }
            matched.push_back(Matched{pimpl->interfaces[itr->second].binding, itr->second, &resource});
        }

        std::sort(matched.begin(), matched.end(), [](const Matched &lhs, const Matched &rhs) {
            return lhs.binding < rhs.binding;
        });
        for (size_t i = 1; i < matched.size(); i++) {
            if (matched[i].binding == matched[i - 1].binding) {
                throw std::runtime_error(
                    std::format(
                        "Compute kernel '{}' received interface '{}' more than once.",
                        pimpl->DisplayName(),
                        pimpl->interfaces[matched[i].index].name
                    )
                );
            }
        }

        // Every declared interface must be supplied. The count check is enough
        // on the success path once duplicates are excluded, so the table is
        // only walked to name a missing interface on the failure path.
        if (matched.size() != pimpl->interfaces.size()) {
            for (const auto &interface : pimpl->interfaces) {
                auto itr = std::lower_bound(
                    matched.begin(), matched.end(), interface.binding, [](const Matched &entry, uint32_t binding) {
                        return entry.binding < binding;
                    }
                );
                if (itr == matched.end() || itr->binding != interface.binding) {
                    throw std::runtime_error(
                        std::format(
                            "Compute kernel '{}' requires a resource for declared interface '{}'.",
                            pimpl->DisplayName(),
                            interface.name
                        )
                    );
                }
            }
        }

        std::vector<Rhi::ResolvedBinding> content;
        content.reserve(matched.size());
        for (const auto &entry : matched) {
            const auto &interface = pimpl->interfaces[entry.index];
            const auto &resource = *entry.resource;

            Rhi::ResolvedBinding binding{};
            binding.binding = interface.binding;
            binding.type = interface.type;
            binding.image_layout = interface.image_layout;

            if (interface.is_image) {
                if (resource.kind != ComputeKernelResource::Kind::Texture) {
                    throw std::runtime_error(
                        std::format(
                            "Compute kernel '{}' interface '{}' is an image, but a buffer was supplied.",
                            pimpl->DisplayName(),
                            interface.name
                        )
                    );
                }
                binding.image_view = resource.image.view;
                binding.sampler = resource.image.sampler;
            } else {
                if (resource.kind != ComputeKernelResource::Kind::Buffer) {
                    throw std::runtime_error(
                        std::format(
                            "Compute kernel '{}' interface '{}' is a buffer, but an image was supplied.",
                            pimpl->DisplayName(),
                            interface.name
                        )
                    );
                }
                binding.buffer = resource.buffer.handle;
                binding.offset = resource.buffer.offset;
                binding.range = resource.buffer.size;
            }
            content.push_back(binding);
        }

        // Re-acquire on every dispatch: the arena's record of the acquiring
        // epoch stays accurate, and the kernel holds no set between dispatches.
        auto set =
            pimpl->device_context->GetDescriptorArena().Acquire(pimpl->pass_info.desc_layout, 0, false, false, content);

        // The kernel records no barrier: synchronization between dispatches is
        // the caller's responsibility.
        cb.bindPipeline(vk::PipelineBindPoint::eCompute, pimpl->pass_info.pipeline.get());
        cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pimpl->pass_info.pipeline_layout.get(), 0, set, {});
        if (push != nullptr && push_size > 0) {
            cb.pushConstants(
                pimpl->pass_info.pipeline_layout.get(),
                vk::ShaderStageFlagBits::eCompute,
                0,
                static_cast<uint32_t>(push_size),
                push
            );
        }
        cb.dispatch(group_count_x, group_count_y, group_count_z);
    }

    struct ComputeKernelCache::impl {
        DeviceContext *device_context{nullptr};
        /// @brief Keyed by module identity; every kernel in it is owned here.
        std::unordered_map<std::string, std::unique_ptr<ComputeKernel>, StringHash, std::equal_to<>> kernels{};
    };

    ComputeKernelCache::ComputeKernelCache(DeviceContext &device_context) : pimpl(std::make_unique<impl>()) {
        pimpl->device_context = &device_context;
    }

    ComputeKernelCache::~ComputeKernelCache() = default;

    ComputeKernel *ComputeKernelCache::Find(std::string_view module_id) noexcept {
        auto itr = pimpl->kernels.find(module_id);
        return itr == pimpl->kernels.end() ? nullptr : itr->second.get();
    }

    ComputeKernel &ComputeKernelCache::Request(
        std::string_view module_id, const std::vector<uint32_t> &spirv_code, std::string_view debug_name
    ) {
        if (ComputeKernel *existing = Find(module_id)) {
            return *existing;
        }

        // The identity is trusted, so the words are touched only here, where
        // they are needed to build the module's one kernel.
        auto kernel = std::unique_ptr<ComputeKernel>(new ComputeKernel(*pimpl->device_context));
        kernel->pimpl->Create(*pimpl->device_context, spirv_code, debug_name.empty() ? module_id : debug_name);

        ComputeKernel &reference = *kernel;
        pimpl->kernels.emplace(std::string{module_id}, std::move(kernel));
        return reference;
    }

    size_t ComputeKernelCache::GetKernelCount() const noexcept {
        return pimpl->kernels.size();
    }
} // namespace Engine::Rhi
