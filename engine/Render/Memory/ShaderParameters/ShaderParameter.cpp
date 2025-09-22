#include "ShaderParameter.h"

namespace Engine::ShdrRfl {
    void ShaderParameters::Assign(const std::string &name, std::variant<uint32_t, int32_t, float> value) noexcept {
        if (std::holds_alternative<uint32_t>(value)) {
            this->arguments[name] = std::get<uint32_t>(value);
        } else if (std::holds_alternative<int32_t>(value)) {
            this->arguments[name] = std::get<int32_t>(value);
        } else if (std::holds_alternative<float>(value)) {
            this->arguments[name] = std::get<float>(value);
        } else {
        }
    }
    void ShaderParameters::Assign(const std::string &name, std::variant<glm::vec4, glm::mat4> value) noexcept {
        if (std::holds_alternative<glm::vec4>(value)) {
            this->arguments[name] = std::get<glm::vec4>(value);
        } else if (std::holds_alternative<glm::mat4>(value)) {
            this->arguments[name] = std::get<glm::mat4>(value);
        } else {
        }
    }
    void ShaderParameters::Assign(const std::string &name, std::shared_ptr <const Texture> tex) noexcept {
        this->interfaces[name] = tex;
    }
    void ShaderParameters::Assign(const std::string &name, std::shared_ptr <const Buffer> buf, size_t offset, size_t size) noexcept {
        this->interfaces[name] = std::make_tuple(buf, offset, size);
    }
    void ShaderParameters::Assign(const std::string &name, const Buffer &buf, size_t offset, size_t size) noexcept {
        this->interfaces[name] = std::make_tuple(std::cref(buf), offset, size);
    }
} // namespace Engine::ShdrRfl
