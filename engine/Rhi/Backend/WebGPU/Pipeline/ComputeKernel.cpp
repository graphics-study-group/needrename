#include "Rhi/Pipeline/ComputeKernel.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Pipeline/ShaderInterfaceDescription.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Texture/Texture.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <format>
#include <map>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief A compute pipeline and the bindings it was built around.
     *
     * The Vulkan kernel reflects its module and derives the binding table from that. This
     * one is handed the table by the bake, which is the point of D9: no reflection library
     * is linked into the build that runs here.
     *
     * `parameter_block_size` is zero and stays zero until a parameter block is bound as a
     * uniform buffer, which is what this backend substitutes for push constants. A caller
     * that dispatches with a value is told rather than having it dropped.
     */
    struct ComputeKernel::impl {
        DeviceContext *device_context{nullptr};
        std::string name{};
        uint32_t parameter_block_size{0};
        PipelineHandle pipeline{};
        PipelineLayoutHandle pipeline_layout{};
        DescriptorSetLayoutHandle set_layout{};
        DescriptorSetLayoutRef set_layout_ref{};
        ShaderInterfaceDescription description{};
        /// Name to the description entry that declares it, for matching a dispatch.
        std::map<std::string, const ShaderInterfaceBinding *> interface_by_name{};
    };

    ComputeKernelResource::ComputeKernelResource(const DeviceBuffer &buffer) {
        kind = Kind::Buffer;
        this->buffer.ref = buffer.GetBufferRef();
        this->buffer.offset = 0u;
        this->buffer.size = std::numeric_limits<size_t>::max();
    }

    ComputeKernelResource ComputeKernelResource::Buffer(const DeviceBuffer &buffer, size_t offset, size_t size) {
        ComputeKernelResource resource(buffer);
        resource.buffer.offset = offset;
        resource.buffer.size = size;
        return resource;
    }

    ComputeKernelResource ComputeKernelResource::Image(Texture &texture, const TextureSubresourceRange &range) {
        ComputeKernelResource resource;
        resource.kind = Kind::Texture;
        resource.image.view = texture.GetViewRef(range);
        resource.image.sampler = texture.GetSamplerRef();
        return resource;
    }

    ComputeKernel::ComputeKernel(DeviceContext &device_context) : pimpl(std::make_unique<impl>()) {
        pimpl->device_context = &device_context;
    }

    ComputeKernel::~ComputeKernel() = default;

    void ComputeKernel::CreateFromModule(
        ShaderModuleHandle module, const ShaderInterfaceDescription &description, std::string_view name
    ) {
        pimpl->name = std::string{name};
        pimpl->description = description;
        // A dispatch names its resources, so each declared interface needs to be findable by
        // name. The map's values point into the description the kernel now owns, which does not
        // move again.
        for (const ShaderInterfaceBinding &declared : pimpl->description.bindings) {
            pimpl->interface_by_name[declared.name] = &declared;
        }
        // One set layout per set the description names. Grouping by set is the shape the
        // Vulkan kernel produces from its reflected table, so a shader that bakes the same
        // way binds the same way.
        std::map<uint32_t, DescriptorSetLayout> sets;
        for (const ShaderInterfaceBinding &binding : description.bindings) {
            DescriptorBinding entry{};
            entry.binding = binding.binding;
            entry.type = ShaderInterfaceDescription::ToDescriptorType(binding.kind);
            entry.count = 1;
            // A storage image's format and access are what the shader declared them as; the
            // bind group can only satisfy them if the layout names the same pair.
            entry.storage_format = binding.storage_format;
            entry.storage_read_only = binding.storage_read_only;
            // A combined binding's sampler goes in the slot the bake recorded: the layout this
            // builds expands the pair into the two bindings the shader declares.
            entry.sampler_binding = binding.sampler_binding;
            sets[binding.set].push_back(entry);
        }

        ImmutableResourceCache &cache = pimpl->device_context->GetIRCache();
        std::vector<DescriptorSetLayoutHandle> set_layouts;
        set_layouts.reserve(sets.size());
        for (auto &[set_index, layout] : sets) {
            static_cast<void>(set_index);
            const DescriptorSetLayoutRef resolved = cache.GetDescriptorSetLayoutRef(layout, name.data());
            set_layouts.push_back(resolved.handle);
            if (!pimpl->set_layout_ref.handle.IsValid()) {
                pimpl->set_layout_ref = resolved;
                // The seam exposes one set layout through the getter; a kernel with several
                // sets reports the first, which is the set its callers bind today.
                pimpl->set_layout = resolved.handle;
            }
        }

        // A kernel that declares a constant block reads it from the group the bake moved it to,
        // so its pipeline layout has to declare that group. The seam spells "this pipeline has a
        // constant block" as a push-constant range, which is where the backend learns the size -
        // the block itself is a uniform buffer here.
        std::vector<Rhi::PushConstantRange> constant_ranges;
        if (description.has_constant_block) {
            constant_ranges.push_back(
                Rhi::PushConstantRange{0, description.constant_block.size, true}
            );
        }
        const PipelineLayoutDescription layout_description{
            .set_layouts = set_layouts, .push_constant_ranges = constant_ranges
        };
        pimpl->pipeline_layout = cache.GetPipelineLayout(layout_description, name.data());

        const WGPUDevice device = ToWgpuDevice(pimpl->device_context->GetDevice());
        WGPUComputePipelineDescriptor pipeline_desc{};
        pipeline_desc.layout = ToWgpuPipelineLayout(pimpl->pipeline_layout);
        pipeline_desc.compute.module = ToWgpuShaderModule(module);
        pipeline_desc.compute.entryPoint =
            WGPUStringView{description.entry_point.data(), description.entry_point.size()};
        if (!name.empty()) {
            pipeline_desc.label = WGPUStringView{name.data(), name.size()};
        }

        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device, &pipeline_desc);
        if (pipeline == nullptr) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "WebGPU: compute pipeline creation failed for '%.*s'.",
                static_cast<int>(name.size()),
                name.data()
            );
        }
        pimpl->pipeline = PipelineHandle::FromValue(reinterpret_cast<uintptr_t>(pipeline));
    }

    void ComputeKernel::Create(const std::vector<uint32_t> &, std::string_view) {
        // Reflecting SPIR-V is what this backend does not do; the bake already did it.
        throw std::logic_error(
            "ComputeKernel::Create takes SPIR-V words, which this backend cannot reflect. Use "
            "CreateFromModule with the interface description the bake produced."
        );
    }

    const std::string &ComputeKernel::GetName() const noexcept {
        return pimpl->name;
    }

    uint32_t ComputeKernel::GetPushConstantSize() const noexcept {
        return pimpl->parameter_block_size;
    }

    PipelineHandle ComputeKernel::GetPipeline() const noexcept {
        return pimpl->pipeline;
    }

    PipelineLayoutHandle ComputeKernel::GetPipelineLayout() const noexcept {
        return pimpl->pipeline_layout;
    }

    DescriptorSetLayoutHandle ComputeKernel::GetDescriptorSetLayout() const noexcept {
        return pimpl->set_layout;
    }

    void ComputeKernel::Dispatch(
        CommandEncoder &encoder,
        ComputeResourceDictionary resources,
        uint32_t group_count_x,
        uint32_t group_count_y,
        uint32_t group_count_z
    ) const {
        if (pimpl->parameter_block_size != 0) {
            throw std::logic_error("This kernel declares a parameter block; dispatch it with a value.");
        }
        DispatchAndRecord(encoder, resources, group_count_x, group_count_y, group_count_z, nullptr, 0);
    }

    void ComputeKernel::DispatchAndRecord(
        CommandEncoder &encoder,
        ComputeResourceDictionary resources,
        uint32_t group_count_x,
        uint32_t group_count_y,
        uint32_t group_count_z,
        const void *push,
        size_t push_size
    ) const {
        if (push != nullptr && push_size != 0) {
            throw std::logic_error(
                "WebGPU: a parameter block is a uniform buffer here, not a push constant. "
                "Dispatch with no value until the block binding is decided."
            );
        }
        if (!pimpl->pipeline.IsValid()) {
            throw std::logic_error("WebGPU: dispatching a kernel that was never created.");
        }

        // Match every supplied name against the description, then order by binding number:
        // the arena keys a bind group on its content, so the caller's dictionary order must
        // not leak into the entries it is handed.
        std::vector<ResolvedBinding> content;
        content.reserve(resources.size());
        for (const auto &[key, resource] : resources) {
            auto itr = pimpl->interface_by_name.find(std::string{key});
            if (itr == pimpl->interface_by_name.end()) {
                throw std::runtime_error(
                    std::format("Compute kernel '{}' does not declare interface '{}'.", pimpl->name, key)
                );
            }
            const ShaderInterfaceBinding &declared = *itr->second;
            ResolvedBinding binding{};
            binding.binding = declared.binding;
            binding.type = ShaderInterfaceDescription::ToDescriptorType(declared.kind);
            const bool is_image = binding.type == DescriptorType::CombinedImageSampler
                                  || binding.type == DescriptorType::SampledImage
                                  || binding.type == DescriptorType::StorageImage
                                  || binding.type == DescriptorType::Sampler;
            if (is_image) {
                if (resource.kind != ComputeKernelResource::Kind::Texture) {
                    throw std::runtime_error(
                        std::format(
                            "Compute kernel '{}' interface '{}' is an image, but a buffer was supplied.",
                            pimpl->name,
                            key
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
                            pimpl->name,
                            key
                        )
                    );
                }
                binding.buffer = resource.buffer.ref;
                binding.offset = resource.buffer.offset;
                binding.range = resource.buffer.size;
            }
            content.push_back(binding);
        }

        std::sort(content.begin(), content.end(), [](const ResolvedBinding &lhs, const ResolvedBinding &rhs) {
            return lhs.binding < rhs.binding;
        });

        // Re-acquire on every dispatch: the arena keys on content, so an identical dictionary
        // reuses one bind group and a changed one does not.
        const DescriptorSetHandle set =
            pimpl->device_context->GetDescriptorArena().Acquire(pimpl->set_layout_ref, 0, false, false, content);

        encoder.BindComputePipeline(pimpl->pipeline, pimpl->pipeline_layout);
        encoder.BindDescriptorSet(0, set);
        encoder.DispatchCompute(group_count_x, group_count_y, group_count_z);
        encoder.EndComputePass();
    }
} // namespace Engine::Rhi


