#ifndef RENDER_ASSET_SHADER_SHADERCOMPILER_INCLUDED
#define RENDER_ASSET_SHADER_SHADERCOMPILER_INCLUDED

#include "Render/render_export.h"
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// Compiling GLSL at run time is the Vulkan runtime's path; build-time compilation is the
// native shader tooling's. Both set ENGINE_SHADER_COMPILER_HAS_GLSLANG; a WebGPU runtime
// build has no compiler at all and ships baked SPIR-V/WGSL instead - that is what keeps
// glslang out of the browser build.
#if defined(ENGINE_SHADER_COMPILER_HAS_GLSLANG)
#include "ShaderIncluder.h"
#include <glslang/Public/ShaderLang.h>
#else
/// The stage selector this header names; glslang's EShLanguage on the shapes that have it.
using EShLanguage = int;
#endif

namespace Engine {

    class RENDER_API ShaderCompiler {
    public:
        ShaderCompiler(const std::filesystem::path &project_assets_path = {});
        ~ShaderCompiler();

        /**
         * @brief Compile GLSL to SPIR-V representation via source code.
         *
         * Due to the way glslang handles inclusion and debug info, non-semantic
         * debug information may be incorrect. Therefore, it is recommended to
         * use the file name overload instead.
         */
        bool CompileGLSLtoSPV(
            std::vector<uint32_t> &spirv,
            const std::string &glsl_code,
            EShLanguage shader_type,
            const std::filesystem::path &shader_directory = std::filesystem::path{}
        );

        /**
         * @brief Compile GLSL to SPIR-V representation via its absolute path.
         *
         * Shader type is inferred from the filename.
         *
         * This overload is tested to generate non-semantic debug information
         * correctly if asked to do so.
         */
        /// @param emit_debug_info Whether to embed non-semantic debug information.
        /// @param included_files Receives the absolute paths read through the include callbacks.
        /// @note Off by default. The extension it adds (`SPV_KHR_non_semantic_info`) is rejected by
        /// a validation layer at `vkCreateShaderModule`, so a build that keeps it on cannot be run
        /// with validation; and a baked archive is meant to carry none (design D12).
        bool CompileGLSLtoSPV(
            std::vector<uint32_t> &spirv,
            const std::filesystem::path &shader_abs_path,
            bool emit_debug_info = false,
            std::set<std::string> *included_files = nullptr
        );

    protected:
        /// @brief Project assets include path, registered as a system include path during compilation.
        std::filesystem::path m_project_assets_path;
#if defined(ENGINE_SHADER_COMPILER_HAS_GLSLANG)
        TBuiltInResource m_built_in_resource = {
            .maxLights = 32,
            .maxClipPlanes = 6,
            .maxTextureUnits = 32,
            .maxTextureCoords = 32,
            .maxVertexAttribs = 64,
            .maxVertexUniformComponents = 4096,
            .maxVaryingFloats = 64,
            .maxVertexTextureImageUnits = 32,
            .maxCombinedTextureImageUnits = 80,
            .maxTextureImageUnits = 32,
            .maxFragmentUniformComponents = 4096,
            .maxDrawBuffers = 32,
            .maxVertexUniformVectors = 128,
            .maxVaryingVectors = 8,
            .maxFragmentUniformVectors = 16,
            .maxVertexOutputVectors = 16,
            .maxFragmentInputVectors = 15,
            .minProgramTexelOffset = -8,
            .maxProgramTexelOffset = 7,
            .maxClipDistances = 8,
            .maxComputeWorkGroupCountX = 65535,
            .maxComputeWorkGroupCountY = 65535,
            .maxComputeWorkGroupCountZ = 65535,
            .maxComputeWorkGroupSizeX = 1024,
            .maxComputeWorkGroupSizeY = 1024,
            .maxComputeWorkGroupSizeZ = 64,
            .maxComputeUniformComponents = 1024,
            .maxComputeTextureImageUnits = 16,
            .maxComputeImageUniforms = 8,
            .maxComputeAtomicCounters = 8,
            .maxComputeAtomicCounterBuffers = 1,
            .maxVaryingComponents = 60,
            .maxVertexOutputComponents = 64,
            .maxGeometryInputComponents = 64,
            .maxGeometryOutputComponents = 128,
            .maxFragmentInputComponents = 128,
            .maxImageUnits = 8,
            .maxCombinedImageUnitsAndFragmentOutputs = 8,
            .maxCombinedShaderOutputResources = 8,
            .maxImageSamples = 0,
            .maxVertexImageUniforms = 0,
            .maxTessControlImageUniforms = 0,
            .maxTessEvaluationImageUniforms = 0,
            .maxGeometryImageUniforms = 0,
            .maxFragmentImageUniforms = 8,
            .maxCombinedImageUniforms = 8,
            .maxGeometryTextureImageUnits = 16,
            .maxGeometryOutputVertices = 256,
            .maxGeometryTotalOutputComponents = 1024,
            .maxGeometryUniformComponents = 1024,
            .maxGeometryVaryingComponents = 64,
            .maxTessControlInputComponents = 128,
            .maxTessControlOutputComponents = 128,
            .maxTessControlTextureImageUnits = 16,
            .maxTessControlUniformComponents = 1024,
            .maxTessControlTotalOutputComponents = 4096,
            .maxTessEvaluationInputComponents = 128,
            .maxTessEvaluationOutputComponents = 128,
            .maxTessEvaluationTextureImageUnits = 16,
            .maxTessEvaluationUniformComponents = 1024,
            .maxTessPatchComponents = 120,
            .maxPatchVertices = 32,
            .maxTessGenLevel = 64,
            .maxViewports = 16,
            .maxVertexAtomicCounters = 0,
            .maxTessControlAtomicCounters = 0,
            .maxTessEvaluationAtomicCounters = 0,
            .maxGeometryAtomicCounters = 0,
            .maxFragmentAtomicCounters = 8,
            .maxCombinedAtomicCounters = 8,
            .maxAtomicCounterBindings = 1,
            .maxVertexAtomicCounterBuffers = 0,
            .maxTessControlAtomicCounterBuffers = 0,
            .maxTessEvaluationAtomicCounterBuffers = 0,
            .maxGeometryAtomicCounterBuffers = 0,
            .maxFragmentAtomicCounterBuffers = 1,
            .maxCombinedAtomicCounterBuffers = 1,
            .maxAtomicCounterBufferSize = 16384,
            .maxTransformFeedbackBuffers = 4,
            .maxTransformFeedbackInterleavedComponents = 64,
            .maxCullDistances = 8,
            .maxCombinedClipAndCullDistances = 8,
            .maxSamples = 4,
            .maxMeshOutputVerticesNV = 256,
            .maxMeshOutputPrimitivesNV = 512,
            .maxMeshWorkGroupSizeX_NV = 32,
            .maxMeshWorkGroupSizeY_NV = 1,
            .maxMeshWorkGroupSizeZ_NV = 1,
            .maxTaskWorkGroupSizeX_NV = 32,
            .maxTaskWorkGroupSizeY_NV = 1,
            .maxTaskWorkGroupSizeZ_NV = 1,
            .maxMeshViewCountNV = 4,
            .maxMeshOutputVerticesEXT = 256,
            .maxMeshOutputPrimitivesEXT = 256,
            .maxMeshWorkGroupSizeX_EXT = 128,
            .maxMeshWorkGroupSizeY_EXT = 128,
            .maxMeshWorkGroupSizeZ_EXT = 128,
            .maxTaskWorkGroupSizeX_EXT = 128,
            .maxTaskWorkGroupSizeY_EXT = 128,
            .maxTaskWorkGroupSizeZ_EXT = 128,
            .maxMeshViewCountEXT = 4,
            .maxDualSourceDrawBuffersEXT = 1,

            .limits = {
                .nonInductiveForLoops = 1,
                .whileLoops = 1,
                .doWhileLoops = 1,
                .generalUniformIndexing = 1,
                .generalAttributeMatrixVectorIndexing = 1,
                .generalVaryingIndexing = 1,
                .generalSamplerIndexing = 1,
                .generalVariableIndexing = 1,
                .generalConstantMatrixVectorIndexing = 1,
            }
        };
#endif // ENGINE_SHADER_COMPILER_HAS_GLSLANG
    };
} // namespace Engine

#endif // RENDER_ASSET_SHADER_SHADERCOMPILER_INCLUDED
