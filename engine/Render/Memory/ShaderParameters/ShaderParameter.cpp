#include "ShaderParameter.h"

#include "../StructuredBuffer.h"
#include <gtc/type_ptr.hpp>

namespace Engine::ShdrRfl {
    struct ShaderParameters::impl {
        StructuredBuffer buffer;
        std::unordered_map <std::string, InterfaceVariant> interfaces;
    };

    ShaderParameters::ShaderParameters() : pimpl(std::make_unique<impl>()) {
    }

    ShaderParameters::~ShaderParameters() noexcept = default;

    void ShaderParameters::Assign(const std::string &name, uint32_t v) noexcept {
        pimpl->buffer.SetVariable(name, v);
    }

    void ShaderParameters::Assign(const std::string &name, float v) noexcept {
        pimpl->buffer.SetVariable(name, v);
    }

    void ShaderParameters::Assign(const std::string &name, const glm::vec4 &v) noexcept {
        pimpl->buffer.SetVariable<const float[4]>(name, glm::value_ptr(v));
    }

    void ShaderParameters::Assign(const std::string &name, const glm::mat4 &v) noexcept {
        pimpl->buffer.SetVariable<const float[16]>(name, glm::value_ptr(v));
    }

    void ShaderParameters::Assign(const std::string &name, float (&v)[4]) noexcept {
        pimpl->buffer.SetVariable<float[4]>(name, v);
    }

    void ShaderParameters::Assign(const std::string &name, float (&v)[16]) noexcept {
        pimpl->buffer.SetVariable<float[16]>(name, v);
    }

    void ShaderParameters::Assign(const std::string &name, std::shared_ptr<const Texture> tex) noexcept {
        pimpl->interfaces[name] = tex;
    }
    void ShaderParameters::Assign(const std::string &name, std::shared_ptr <const DeviceBuffer> buf, size_t offset, size_t size) noexcept {
        pimpl->interfaces[name] = std::make_tuple(buf, offset, size);
    }
    void ShaderParameters::Assign(
                const std::string & name, 
                std::shared_ptr <const ComputeBuffer> buf
    ) noexcept {
        pimpl->interfaces[name] = buf;
    }
    void ShaderParameters::Assign(const std::string &name, const ComputeBuffer &buf) noexcept {
        pimpl->interfaces[name] = std::cref(buf);
    }
    void ShaderParameters::Assign(
        const std::string &name, const DeviceBuffer &buf, size_t offset, size_t size
    ) noexcept {
        pimpl->interfaces[name] = std::make_tuple(std::cref(buf), offset, size);
    }
    auto ShaderParameters::GetInterfaces() const noexcept -> const std::unordered_map <std::string, ShaderParameters::InterfaceVariant> & {
        return pimpl->interfaces;
    }
    const StructuredBuffer &ShaderParameters::GetStructuredBuffer() const noexcept {
        return pimpl->buffer;
    }
} // namespace Engine::ShdrRfl
