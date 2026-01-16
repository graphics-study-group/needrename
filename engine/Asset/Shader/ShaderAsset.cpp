#include "ShaderAsset.h"
#include <SDL3/SDL.h>
#include <fstream>

#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <Asset/Shader/ShaderCompiler.h>
#include <MainClass.h>
#include <Reflection/serialization.h>

namespace Engine {
    void ShaderAsset::save_asset_to_archive(Serialization::Archive &archive) const {
        auto &json = *archive.m_cursor;
        size_t glsl_extra_data_id = archive.create_new_extra_data_buffer(".glsl");
        json["glsl_extra_data_id"] = glsl_extra_data_id;
        size_t binary_extra_data_id = archive.create_new_extra_data_buffer(".spv");
        json["binary_extra_data_id"] = binary_extra_data_id;

        if (storeType == StoreType::GLSL) {
            // Ignore GLSL shader save
        } else if (storeType == StoreType::SPIRV) {
            auto &data = archive.m_context->extra_data[binary_extra_data_id];
            size_t file_size = binary.size() * sizeof(uint32_t);
            data.resize(file_size);
            std::memcpy(data.data(), binary.data(), file_size);
        } else {
            throw std::runtime_error("Unknown shader store type");
        }

        Asset::save_asset_to_archive(archive);
    }

    void ShaderAsset::load_asset_from_archive(Serialization::Archive &archive) {
        Asset::load_asset_from_archive(archive);
        auto &json = *archive.m_cursor;

        if (storeType == StoreType::GLSL) {
            // Don't load GLSL source from asset, instead load it directly from file.
            bool compile_res = Compile();
            assert(compile_res);
        } else if (storeType == StoreType::SPIRV) {
            auto &data = archive.m_context->extra_data[json["binary_extra_data_id"].get<size_t>()];
            size_t file_size = data.size();
            assert(file_size > 0);
            binary.resize((file_size - sizeof(uint32_t) + 1) / sizeof(uint32_t) + 1);
            std::memcpy(binary.data(), data.data(), file_size);
        } else {
            throw std::runtime_error("Unknown shader store type");
        }
    }

    void ShaderAsset::LoadFromFile(
        const std::filesystem::path &path, ShaderType type, const std::string &name, const std::string &entry_point
    ) {
        shaderType = type;

        m_name = name.empty() ? path.stem().string() : name;
        m_entry_point = entry_point;

        if (path.extension() == ".glsl") {
            storeType = StoreType::GLSL;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            assert(file.is_open());
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_APPLICATION, "Loading shader %s, size %llu bytes\n", path.string().c_str(), file_size
            );
            glsl_code.resize(file_size);
            file.read(glsl_code.data(), file_size);
            file.close();
            bool compile_res = Compile(path);
            assert(compile_res);
        } else if (path.extension() == ".spv") {
            storeType = StoreType::SPIRV;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            assert(file.is_open());
            size_t file_size = file.tellg();
            file.seekg(0, std::ios::beg);
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_APPLICATION, "Loading shader %s, size %llu bytes\n", path.string().c_str(), file_size
            );
            binary.resize((file_size - sizeof(uint32_t) + 1) / sizeof(uint32_t) + 1);
            file.read(reinterpret_cast<char *>(binary.data()), file_size);
            file.close();
        } else {
            throw std::runtime_error("Unknown shader file extension: " + path.string());
        }
    }

    bool ShaderAsset::Compile(std::filesystem::path shader_path_abs) {
        if (binary.size() > 0) {
            // Already compiled
            return true;
        }
        EShLanguage type;
        switch (shaderType) {
        case ShaderType::Vertex:
            type = EShLangVertex;
            break;
        case ShaderType::Fragment:
            type = EShLangFragment;
            break;
        case ShaderType::Compute:
            type = EShLangCompute;
            break;
        case ShaderType::TessellationControl:
            type = EShLangTessControl;
            break;
        case ShaderType::TessellationEvaluation:
            type = EShLangTessEvaluation;
            break;
        case ShaderType::Geometry:
            type = EShLangGeometry;
            break;
        default:
            throw std::runtime_error("Unsupported shader type for compilation");
        }
        
        if (shader_path_abs.empty()) {
            auto fs_db = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());
            assert(fs_db);
            shader_path_abs = fs_db->ProjectPathToFilesystemPath(fs_db->GetAssetPath(GetGUID()));
            // XXX: We need to generalize here.
            shader_path_abs = shader_path_abs.replace_extension(
                "0.glsl"
            );
        }
        assert(shader_path_abs.is_absolute());
        return MainClass::GetInstance()->GetShaderCompiler()->CompileGLSLtoSPV(binary, shader_path_abs);
    }
} // namespace Engine
