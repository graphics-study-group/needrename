#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

// 初始化 glslang 库
void InitGlslang() {
    glslang::InitializeProcess();
}

// 释放 glslang 库
void FinalizeGlslang() {
    glslang::FinalizeProcess();
}

bool CompileGLSLtoSPV(const std::string &shaderSource, EShLanguage shaderType, std::vector<uint32_t> &spirv) {
    const char *shaderStrings[1];
    shaderStrings[0] = shaderSource.c_str();

    glslang::TShader shader(shaderType);
    shader.setStrings(shaderStrings, 1);

    // 设置默认资源限制
    TBuiltInResource resources = {};
    resources.maxLights = 32;
    resources.maxClipPlanes = 6;
    resources.maxTextureUnits = 32;
    resources.maxTextureCoords = 32;
    resources.maxVertexAttribs = 64;
    resources.maxVertexUniformComponents = 4096;
    resources.maxVaryingFloats = 64;
    resources.maxVertexTextureImageUnits = 32;
    resources.maxCombinedTextureImageUnits = 80;
    resources.maxTextureImageUnits = 32;
    resources.maxFragmentUniformComponents = 4096;
    resources.maxDrawBuffers = 32;
    resources.maxVertexUniformVectors = 128;
    resources.maxVaryingVectors = 8;
    resources.maxFragmentUniformVectors = 16;
    resources.maxVertexOutputVectors = 16;
    resources.maxFragmentInputVectors = 15;
    resources.minProgramTexelOffset = -8;
    resources.maxProgramTexelOffset = 7;
    resources.maxClipDistances = 8;
    resources.maxComputeWorkGroupCountX = 65535;
    resources.maxComputeWorkGroupCountY = 65535;
    resources.maxComputeWorkGroupCountZ = 65535;
    resources.maxComputeWorkGroupSizeX = 1024;
    resources.maxComputeWorkGroupSizeY = 1024;
    resources.maxComputeWorkGroupSizeZ = 64;
    resources.maxComputeUniformComponents = 1024;
    resources.maxComputeTextureImageUnits = 16;
    resources.maxComputeImageUniforms = 8;
    resources.maxComputeAtomicCounters = 8;
    resources.maxComputeAtomicCounterBuffers = 1;
    resources.maxVaryingComponents = 60;
    resources.maxVertexOutputComponents = 64;
    resources.maxGeometryInputComponents = 64;
    resources.maxGeometryOutputComponents = 128;
    resources.maxFragmentInputComponents = 128;
    resources.maxImageUnits = 8;
    resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    resources.maxCombinedShaderOutputResources = 8;
    resources.maxImageSamples = 0;
    resources.maxVertexImageUniforms = 0;
    resources.maxTessControlImageUniforms = 0;
    resources.maxTessEvaluationImageUniforms = 0;
    resources.maxGeometryImageUniforms = 0;
    resources.maxFragmentImageUniforms = 8;
    resources.maxCombinedImageUniforms = 8;
    resources.maxGeometryTextureImageUnits = 16;
    resources.maxGeometryOutputVertices = 256;
    resources.maxGeometryTotalOutputComponents = 1024;
    resources.maxGeometryUniformComponents = 1024;
    resources.maxGeometryVaryingComponents = 64;
    resources.maxTessControlInputComponents = 128;
    resources.maxTessControlOutputComponents = 128;
    resources.maxTessControlTextureImageUnits = 16;
    resources.maxTessControlUniformComponents = 1024;
    resources.maxTessControlTotalOutputComponents = 4096;
    resources.maxTessEvaluationInputComponents = 128;
    resources.maxTessEvaluationOutputComponents = 128;
    resources.maxTessEvaluationTextureImageUnits = 16;
    resources.maxTessEvaluationUniformComponents = 1024;
    resources.maxTessPatchComponents = 120;
    resources.maxPatchVertices = 32;
    resources.maxTessGenLevel = 64;
    resources.maxViewports = 16;
    resources.maxVertexAtomicCounters = 0;
    resources.maxTessControlAtomicCounters = 0;
    resources.maxTessEvaluationAtomicCounters = 0;
    resources.maxGeometryAtomicCounters = 0;
    resources.maxFragmentAtomicCounters = 8;
    resources.maxCombinedAtomicCounters = 8;
    resources.maxAtomicCounterBindings = 1;
    resources.maxVertexAtomicCounterBuffers = 0;
    resources.maxTessControlAtomicCounterBuffers = 0;
    resources.maxTessEvaluationAtomicCounterBuffers = 0;
    resources.maxGeometryAtomicCounterBuffers = 0;
    resources.maxFragmentAtomicCounterBuffers = 1;
    resources.maxCombinedAtomicCounterBuffers = 1;
    resources.maxAtomicCounterBufferSize = 16384;
    resources.maxTransformFeedbackBuffers = 4;
    resources.maxTransformFeedbackInterleavedComponents = 64;
    resources.maxCullDistances = 8;
    resources.maxCombinedClipAndCullDistances = 8;
    resources.maxSamples = 4;

    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(&resources, 100, false, messages)) {
        std::cerr << "GLSL Parsing Failed:\n" << shader.getInfoLog() << "\n" << shader.getInfoDebugLog() << std::endl;
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(messages)) {
        std::cerr << "GLSL Linking Failed:\n" << program.getInfoLog() << "\n" << program.getInfoDebugLog() << std::endl;
        return false;
    }

    glslang::GlslangToSpv(*program.getIntermediate(shaderType), spirv);

    return true;
}

int main() {
    InitGlslang();

    std::string vertexShader = R"(
        #version 450
        layout(location = 0) in vec3 inPosition;
        void main() {
            gl_Position = vec4(inPosition, 1.0);
        }
    )";

    std::vector<uint32_t> spirv;
    if (CompileGLSLtoSPV(vertexShader, EShLangVertex, spirv)) {
        std::cout << "Shader compiled successfully! SPIR-V size: " << spirv.size() << " words\n";
    } else {
        std::cerr << "Shader compilation failed!\n";
    }

    FinalizeGlslang();
    return 0;
}
