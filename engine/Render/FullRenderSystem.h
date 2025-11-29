#ifndef RENDER_FULLRENDERSYSTEM
#define RENDER_FULLRENDERSYSTEM

/** THIS FILE IS USED AS INTERFACE ONLY, DO NOT INCLUDE IT IN THE INTERNAL ENGINE LIBRARY. **/

#include <vulkan/vulkan.hpp>

#include "Render/AttachmentUtilsFunc.h"
#include "Render/ImageUtilsFunc.h"

#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/MaterialRegistry.h"
#include "Render/RenderSystem/RendererManager.h"
#include "Render/RenderSystem/SamplerManager.h"
#include "Render/RenderSystem/SceneDataManager.h"
#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/RenderSystem/Swapchain.h"

#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/ComputeCommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/ComputeContext.h"
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/GraphicsContext.h"

#include "Render/Pipeline/Compute/ComputeStage.h"

#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"

#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialTemplate.h"
#include "Render/Pipeline/Material/Templates/BlinnPhong.h"

#include "Render/Renderer/Camera.h"
#include "Render/Renderer/HomogeneousMesh.h"

#include "Render/Memory/Texture.h"
#include "Render/Memory/RenderTargetTexture.h"
#include "Render/Memory/ImageTexture.h"

#include "Render/RenderSystem.h"

#endif // RENDER_FULLRENDERSYSTEM
