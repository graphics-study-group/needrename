/**
 * @file rhi_public_headers_test.cpp
 * @brief Compile every public Rhi header without a graphics API on the include path.
 *
 * This translation unit exists to fail the build, not the run: it includes the whole
 * public surface of `engine/Rhi`, and its target links only the engine header
 * interface — no Vulkan include directory, no VMA, no glslang, no SPIRV-Cross. A
 * backend type that leaks back into a public header breaks the build here rather
 * than in whichever consumer happens to include it next.
 */

#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Buffer/IndexedBuffer.h"
#include "Rhi/Buffer/StructuredBuffer.h"
#include "Rhi/Buffer/StructuredBufferPlacer.h"
#include "Rhi/Command/CommandEncoder.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/BindingRef.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryAccessTypes.h"
#include "Rhi/Device/MemoryAllocation.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include "Rhi/Pipeline/InterfaceBindingResolver.h"
#include "Rhi/Pipeline/PipelineEnums.h"
#include "Rhi/Pipeline/PipelineDescription.h"
#include "Rhi/Pipeline/ShaderInterface.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Pipeline/ShaderResourceBinding.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/Submission/EpochTracker.h"
#include "Rhi/Submission/SubmissionHelper.h"
#include "Rhi/Texture/ImageTexture.h"
#include "Rhi/Texture/ImageUtils.h"
#include "Rhi/Texture/Texture.h"
#include "Rhi/Texture/TextureSubresourceView.h"

int main() {
    return 0;
}

