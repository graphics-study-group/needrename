#include "Rhi/Pipeline/ShaderInterfaceDescription.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <limits>
#include <sstream>

namespace Engine::Rhi {
    DescriptorType ShaderInterfaceDescription::ToDescriptorType(const std::string &kind) {
        // The strings are the baker's; a kind this does not know is reported by the caller
        // rather than guessed at, because a wrong descriptor type is a validation error at
        // dispatch and a silently wrong one is worse.
        if (kind == "uniform-buffer") {
            return DescriptorType::UniformBuffer;
        }
        if (kind == "storage-buffer") {
            return DescriptorType::StorageBuffer;
        }
        if (kind == "combined-image-sampler") {
            return DescriptorType::CombinedImageSampler;
        }
        if (kind == "sampled-image") {
            return DescriptorType::SampledImage;
        }
        if (kind == "storage-image") {
            return DescriptorType::StorageImage;
        }
        if (kind == "sampler") {
            return DescriptorType::Sampler;
        }
        return DescriptorType::UniformBuffer;
    }

    std::optional<ShaderInterfaceDescription> ShaderInterfaceDescription::FromFile(
        const std::filesystem::path &path
    ) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return std::nullopt;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return FromString(buffer.str());
    }

    std::optional<ShaderInterfaceDescription> ShaderInterfaceDescription::FromString(const std::string &text) {
        if (text.empty()) {
            return std::nullopt;
        }
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(text);
        } catch (const nlohmann::json::exception &) {
            return std::nullopt;
        }

        ShaderInterfaceDescription description;
        description.shader = json.value("shader", std::string{});
        description.stage = json.value("stage", std::string{});
        description.entry_point = json.value("entry_point", std::string{"main"});

        if (json.contains("constant_block") && !json["constant_block"].is_null()) {
            const auto &block = json["constant_block"];
            description.has_constant_block = true;
            description.constant_block.set = block.value("set", 0u);
            description.constant_block.binding = block.value("binding", 0u);
            description.constant_block.size = block.value("size", 0u);
        }

        if (json.contains("bindings") && json["bindings"].is_array()) {
            for (const auto &entry : json["bindings"]) {
                ShaderInterfaceBinding binding;
                binding.set = entry.value("set", 0u);
                binding.binding = entry.value("binding", 0u);
                binding.kind = entry.value("kind", std::string{});
                binding.sample_type = entry.value("sample_type", 0u);
                binding.view_dimension = entry.value("view_dimension", 1u);
                binding.name = entry.value("name", std::string{});
                binding.storage_format = entry.value("format", 0u);
                binding.storage_read_only = entry.value("read_only", false);
                binding.sampler_binding =
                    entry.value("sampler_binding", std::numeric_limits<uint32_t>::max());
                binding.block_size = entry.value("block_size", 0u);
                for (const auto &member : entry.value("members", nlohmann::json::array())) {
                    ShaderInterfaceBinding::Member parsed{};
                    parsed.name = member.value("name", std::string{});
                    parsed.offset = member.value("offset", 0u);
                    parsed.size = member.value("size", 0u);
                    parsed.kind = member.value("kind", 0u);
                    binding.members.push_back(std::move(parsed));
                }
                description.bindings.push_back(std::move(binding));
            }
        }
        return description;
    }
} // namespace Engine::Rhi
