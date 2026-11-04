#include "Rhi/Pipeline/ComputeKernel.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Backend/WebGPU/WgpuStructs.h"
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
        std::map<uint32_t, DescriptorSetLayoutRef> set_layouts_by_index;
        ShaderInterfaceDescription description{};
        /// Name to the description entry that declares it, for matching a dispatch.
        std::map<std::string, const ShaderInterfaceBinding *> interface_by_name{};
        /// A set with nothing bound, used for the groups a padded pipeline layout declares.
        ///
        /// The constant block lives at group 3, so groups 1 and 2 exist in every kernel's
        /// pipeline layout whether or not the kernel has anything to put there - and the
        /// interface requires every group of a layout to be bound before a dispatch. This is
        /// what those groups get.
        DescriptorSetLayoutHandle empty_set_layout{};
        /// @brief Whether the device built this kernel's pipeline.
        ///
        /// True until a creation says otherwise: a kernel that reaches a dispatch without having
        /// gone through `CreateFromModule` has not been refused by anything, and silently skipping
        /// its work would turn a missing call into a black frame.
        bool available{true};
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
        pimpl->empty_set_layout = pimpl->device_context->GetIRCache().GetDescriptorSetLayout(
            DescriptorSetLayout{}, "Empty Descriptor Set Layout"
        );
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
        std::vector<DescriptorSetLayoutHandle> set_layouts(k_constant_bind_group);
        for (auto &slot : set_layouts) {
            slot = cache.GetDescriptorSetLayoutRef(DescriptorSetLayout{}, "Empty Descriptor Set Layout", true).handle;
        }
        for (auto &[set_index, layout] : sets) {
            if (set_index >= k_constant_bind_group) {
                continue;
            }
            // One stage: these bindings belong to a compute pipeline, and a layout that also claims
            // the fragment stage is one the device may refuse for the number of storage buffers.
            const DescriptorSetLayoutRef resolved = cache.GetDescriptorSetLayoutRef(layout, name.data(), true);
            set_layouts[set_index] = resolved.handle;
            pimpl->set_layouts_by_index[set_index] = resolved;
            if (!pimpl->set_layout_ref.handle.IsValid()) {
                pimpl->set_layout_ref = resolved;
                pimpl->set_layout = resolved.handle;
            }
        }

        // A kernel that declares a constant block reads it from the group the bake moved it to,
        // so its pipeline layout has to declare that group. The seam spells "this pipeline has a
        // constant block" as a push-constant range, which is where the backend learns the size -
        // the block itself is a uniform buffer here.
        std::vector<Rhi::PushConstantRange> constant_ranges;
        if (description.has_constant_block) {
            // The size the caller is held to is the block the bake recorded: a kernel's `Dispatch`
            // asserts that what it pushes fits, and a size of zero would refuse every push.
            pimpl->parameter_block_size = description.constant_block.size;
            constant_ranges.push_back(Rhi::PushConstantRange{0, description.constant_block.size, true});
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
        if (push != nullptr && push_size != 0 && !pimpl->description.has_constant_block) {
            throw std::logic_error("WebGPU: kernel was dispatched with a parameter block it does not declare.");
        }
        if (!pimpl->pipeline.IsValid()) {
            throw std::logic_error("WebGPU: dispatching a kernel that was never created.");
        }
        if (!pimpl->available) {
            // The device refused this kernel, so there is nothing to record. The refusal was
            // logged when the pipeline came back empty, so this path is silent.
            return;
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
            const bool is_image =
                binding.type == DescriptorType::CombinedImageSampler || binding.type == DescriptorType::SampledImage
                || binding.type == DescriptorType::StorageImage || binding.type == DescriptorType::Sampler;
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

        // WebGPU rejects a dispatch that reaches one buffer through two storage bindings when
        // either of them is writable ("writable storage buffer binding aliasing"). Vulkan accepts
        // it, and wgpu-native accepts it too, so a browser-only rule would otherwise stay
        // undiscovered until the page runs it. Checking it here makes the desktop validation build
        // fail the same way the browser does.
        {
            struct StorageUse {
                uint32_t set{0};
                uint32_t binding{0};
                std::string name{};
                bool read_only{true};
            };
            std::map<uint64_t, StorageUse> uses;
            for (const auto &[key, resource] : resources) {
                auto itr = pimpl->interface_by_name.find(std::string{key});
                if (itr == pimpl->interface_by_name.end()) {
                    continue; // already reported above
                }
                const ShaderInterfaceBinding &declared = *itr->second;
                if (declared.kind != "storage-buffer") {
                    continue;
                }
                const StorageUse use{declared.set, declared.binding, declared.name, declared.storage_read_only};
                const uint64_t identity = resource.buffer.ref.id.GetValue();
                const auto [existing, inserted] = uses.emplace(identity, use);
                if (!inserted && (!existing->second.read_only || !use.read_only)) {
                    throw std::runtime_error(
                        std::format(
                            "WebGPU: compute kernel '{}' binds one buffer (identity {}) to two storage "
                            "bindings of one dispatch - set {} binding {} ('{}') and set {} binding {} "
                            "('{}') - and at least one is writable. WebGPU forbids that aliasing; use "
                            "one read-write binding or two buffers.",
                            pimpl->name,
                            identity,
                            existing->second.set,
                            existing->second.binding,
                            existing->second.name,
                            use.set,
                            use.binding,
                            use.name
                        )
                    );
                }
            }
        }

        // Re-acquire on every dispatch: the arena keys on content, so an identical dictionary
        // reuses one bind group and a changed one does not.
        encoder.BindComputePipeline(pimpl->pipeline, pimpl->pipeline_layout);
        for (const auto &[set_index, layout_ref] : pimpl->set_layouts_by_index) {
            std::vector<ResolvedBinding> entries;
            for (const auto &[key, resource] : resources) {
                static_cast<void>(resource);
                auto itr = pimpl->interface_by_name.find(std::string{key});
                if (itr == pimpl->interface_by_name.end() || itr->second->set != set_index) {
                    continue;
                }
                for (const ResolvedBinding &entry : content) {
                    if (entry.binding == itr->second->binding) {
                        entries.push_back(entry);
                        break;
                    }
                }
            }
            std::sort(entries.begin(), entries.end(), [](const ResolvedBinding &lhs, const ResolvedBinding &rhs) {
                return lhs.binding < rhs.binding;
            });
            const DescriptorSetHandle set =
                pimpl->device_context->GetDescriptorArena().Acquire(layout_ref, 0, false, false, entries);
            encoder.BindDescriptorSet(set_index, set);
        }
        if (push != nullptr && push_size != 0) {
            encoder.SetKernelParams(std::span{static_cast<const std::byte *>(push), static_cast<size_t>(push_size)});
        }
        encoder.DispatchCompute(group_count_x, group_count_y, group_count_z);
        encoder.EndComputePass();
    }
} // namespace Engine::Rhi
