#ifndef RENDER_FULLRENDERSYSTEM
#define RENDER_FULLRENDERSYSTEM

/** THIS FILE IS USED AS INTERFACE ONLY, DO NOT INCLUDE IT IN THE INTERNAL ENGINE LIBRARY. **/

#include <vulkan/vulkan.hpp>

#include "Render/AttachmentUtilsFunc.h"
#include "Rhi/ImageUtilsFunc.h"

#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/ResizableRTTManager.h"
#include "Render/RenderSystem/SceneDataManager.h"
#include "Render/Resource/AllRenderResourceManagers.h"
#include "Render/Resource/StaticMeshResource.h"
#include "Rhi/DeviceInterface.h"
#include "Rhi/ImmutableResourceCache.h"
#include "Rhi/Structs.h"
#include "Rhi/SubmissionHelper.h"

#include "Render/Pipeline/CommandBuffer.h"

#include "Rhi/ComputeResourceBinding.h"
#include "Rhi/ComputeStage.h"

#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"

#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/Pipeline/Material/MaterialTemplate.h"

#include "Render/Renderer/Camera.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"
#include "Render/Renderer/VertexAttribute.h"

#include "Rhi/MemoryAccessTypes.h"
#include "Rhi/MemoryTypes.h"

#include "Render/Memory/RenderTargetTexture.h"
#include "Rhi/ImageTexture.h"
#include "Rhi/Texture.h"
#include "Rhi/TextureSubresourceView.h"

#include "Rhi/ComputeBuffer.h"
#include "Rhi/DeviceBuffer.h"
#include "Rhi/ShaderResourceBinding.h"
#include "Rhi/StructuredBuffer.h"
#include "Rhi/StructuredBufferPlacer.h"

#include "Render/RenderSystem.h"

#endif // RENDER_FULLRENDERSYSTEM
