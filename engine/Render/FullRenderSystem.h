#ifndef ENGINE_RENDER_FULLRENDERSYSTEM_INCLUDED
#define ENGINE_RENDER_FULLRENDERSYSTEM_INCLUDED

/** THIS FILE IS USED AS INTERFACE ONLY, DO NOT INCLUDE IT IN THE INTERNAL ENGINE LIBRARY. **/

#include <vulkan/vulkan.hpp>

#include "Render/ConstantData/PerCameraConstants.h"
#include "Render/ConstantData/PerModelConstants.h"
#include "Render/ConstantData/PerSceneConstants.h"

#include "Render/RenderSystem/Structs.h"
#include "Render/RenderSystem/Swapchain.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include "Render/RenderSystem/MaterialRegistry.h"
#include "Render/RenderSystem/RendererManager.h"

#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/GraphicsContext.h"
#include "Render/Pipeline/CommandBuffer/ComputeContext.h"
#include "Render/Pipeline/CommandBuffer/GraphicsCommandBuffer.h"
#include "Render/Pipeline/CommandBuffer/ComputeCommandBuffer.h"
#include "Render/Pipeline/RenderTargetBinding.h"

#include "Render/Pipeline/Compute/ComputeStage.h"

#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialTemplate.h"
#include "Render/Pipeline/Material/Templates/BlinnPhong.h"

#include "Render/Renderer/HomogeneousMesh.h"
#include "Render/Renderer/Camera.h"

#include "Render/Memory/Texture.h"
#include "Render/Memory/SampledTexture.h"
#include "Render/Memory/SampledTextureInstantiated.h"

#include "Render/RenderSystem.h"

#endif // ENGINE_RENDER_FULLRENDERSYSTEM_INCLUDED
