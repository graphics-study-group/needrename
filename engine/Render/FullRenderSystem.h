#ifndef RENDER_FULLRENDERSYSTEM
#define RENDER_FULLRENDERSYSTEM

/** THIS FILE IS USED AS INTERFACE ONLY, DO NOT INCLUDE IT IN THE INTERNAL ENGINE LIBRARY. **/

#include "Render/render_export.h"
#include <vulkan/vulkan.hpp>

#include "Render/AttachmentUtilsFunc.h"
#include "Rhi/Texture/ImageUtilsFunc.h"

#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/ResizableRTTManager.h"
#include "Render/RenderSystem/SceneDataManager.h"
#include "Render/Resource/AllRenderResourceManagers.h"
#include "Render/Resource/StaticMeshResource.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/Structs.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Submission/SubmissionHelper.h"

#include "Render/Pipeline/CommandBuffer.h"

#include "Rhi/Pipeline/ComputeResourceBinding.h"
#include "Rhi/Pipeline/ComputeStage.h"

#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"

#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/Pipeline/Material/MaterialTemplate.h"

#include "Render/Renderer/Camera.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"
#include "Render/Renderer/VertexAttribute.h"

#include "Rhi/Device/MemoryAccessTypes.h"
#include "Rhi/Device/MemoryTypes.h"

#include "Render/Memory/RenderTargetTexture.h"
#include "Rhi/Texture/ImageTexture.h"
#include "Rhi/Texture/Texture.h"
#include "Rhi/Texture/TextureSubresourceView.h"

#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Buffer/StructuredBuffer.h"
#include "Rhi/Buffer/StructuredBufferPlacer.h"
#include "Rhi/Pipeline/ShaderResourceBinding.h"

#include "Render/RenderSystem.h"

#endif // RENDER_FULLRENDERSYSTEM
