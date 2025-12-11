#ifndef TOON_SHADING_RENDERING
#define TOON_SHADING_RENDERING

#include <memory>

#include <UserInterface/GUISystem.h>
#include <Render/FullRenderSystem.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/MaterialTemplateAsset.h>

#include <cmake_config.h>

const std::unordered_map <const char *, const char *> SHADER_FILENAME_MAP = {
    {"BackfacingVert", "shaders/toon_outline.vert.0.spv"},
    {"BackfacingFrag", "shaders/toon_outline.frag.0.spv"},
    {"MainVert", "shaders/toon_main.vert.0.spv"},
    {"MainFrag", "shaders/toon_main.frag.0.spv"}
};

struct RampControlPoint {
    float percentage;
    uint8_t r, g, b;
};

/**
 * @brief Fill a ramp map with control points.
 * Assume 4-channel linear RGBA format, 1 byte per channel.
 */
std::vector <uint8_t> FillRampMap(uint32_t width, const std::vector <RampControlPoint> & cp) {
    std::vector <uint8_t> data;
    data.resize(4 * width);

    auto current_cp = cp.begin();
    for (size_t i = 0; i < width; i++) {
        float percentage = i * 1.0f / width;
        if (std::next(current_cp) != cp.end() && std::next(current_cp)->percentage < percentage) {
            current_cp = std::next(current_cp);
        }

        data[4 * i] = current_cp->r;
        data[4 * i + 1] = current_cp->g;
        data[4 * i + 2] = current_cp->b;
        data[4 * i + 3] = 0;
    }
    return data;
}

/**
 * @brief Create a ramp map with control points.
 * Assume 4-channel linear RGBA format, 1 byte per channel.
 */
std::unique_ptr <Engine::ImageTexture> CreateRampMapTexture(Engine::RenderSystem & system, uint32_t width) {
    auto ptr = Engine::ImageTexture::CreateUnique(
        system,
        Engine::ImageTexture::ImageTextureDesc{
            .dimensions = 1,
            .width = width, .height = 1, .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = Engine::ImageTexture::ITFormat::R8G8B8A8UNorm,
            .is_cube_map = false
        },
        Engine::ImageTexture::SamplerDesc{
            .u_address = Engine::ImageTexture::SamplerDesc::AddressMode::ClampToEdge,
            .v_address = Engine::ImageTexture::SamplerDesc::AddressMode::ClampToEdge,
        },
        "Ramp map"
    );
    return ptr;
}

std::unique_ptr <Engine::MaterialTemplateAsset> GetOutlinePassTemplate() {
    using namespace Engine;
    std::unique_ptr asset = std::make_unique<Engine::MaterialTemplateAsset>();
    auto vs = std::make_shared<ShaderAsset>();
    auto fs = std::make_shared<ShaderAsset>();
    vs->LoadFromFile(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / SHADER_FILENAME_MAP.at("BackfacingVert"), ShaderAsset::ShaderType::Vertex);
    fs->LoadFromFile(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / SHADER_FILENAME_MAP.at("BackfacingFrag"), ShaderAsset::ShaderType::Fragment);
    asset->name = "Backfacing Outline Pass";

    asset->properties.attachments.color = {Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm};
    asset->properties.attachments.depth = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    asset->properties.attachments.stencil = Engine::ImageUtils::ImageFormat::UNDEFINED;
    asset->properties.attachments.color_blending = {Engine::PipelineProperties::ColorBlendingProperties{}};

    asset->properties.depth_stencil.depth_test_enable = true;
    asset->properties.depth_stencil.depth_write_enable = true;
    asset->properties.depth_stencil.depth_comparator = PipelineUtils::DSComparator::Less;

    asset->properties.rasterizer.culling = PipelineUtils::CullingMode::Front;
    asset->properties.rasterizer.front = PipelineUtils::FrontFace::Counterclockwise;

    asset->properties.shaders.shaders = {std::make_shared<AssetRef>(vs), std::make_shared<AssetRef>(fs)};
    return asset;
}

std::unique_ptr <Engine::MaterialTemplateAsset> GetMainPassTemplate() {
    using namespace Engine;
    std::unique_ptr asset = std::make_unique<Engine::MaterialTemplateAsset>();
    auto vs = std::make_shared<ShaderAsset>();
    auto fs = std::make_shared<ShaderAsset>();
    vs->LoadFromFile(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / SHADER_FILENAME_MAP.at("MainVert"), ShaderAsset::ShaderType::Vertex);
    fs->LoadFromFile(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / SHADER_FILENAME_MAP.at("MainFrag"), ShaderAsset::ShaderType::Fragment);
    asset->name = "Backfacing Outline Pass";

    asset->properties.attachments.color = {Engine::ImageUtils::ImageFormat::R8G8B8A8UNorm};
    asset->properties.attachments.depth = Engine::ImageUtils::ImageFormat::D32SFLOAT;
    asset->properties.attachments.stencil = Engine::ImageUtils::ImageFormat::UNDEFINED;
    asset->properties.attachments.color_blending = {Engine::PipelineProperties::ColorBlendingProperties{}};

    asset->properties.depth_stencil.depth_test_enable = true;
    asset->properties.depth_stencil.depth_write_enable = true;
    asset->properties.depth_stencil.depth_comparator = PipelineUtils::DSComparator::Less;

    asset->properties.rasterizer.culling = PipelineUtils::CullingMode::Back;
    asset->properties.rasterizer.front = PipelineUtils::FrontFace::Counterclockwise;

    asset->properties.shaders.shaders = {std::make_shared<AssetRef>(vs), std::make_shared<AssetRef>(fs)};
    return asset;
}

std::unique_ptr <Engine::MaterialLibraryAsset> GetMaterialLibraryAsset() {
    using namespace Engine;
    std::shared_ptr outline_pass = GetOutlinePassTemplate();
    std::shared_ptr main_pass = GetMainPassTemplate();

    std::unique_ptr library_asset = std::make_unique<Engine::MaterialLibraryAsset>();
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = std::make_shared<AssetRef>(outline_pass);
    library_asset->material_bundle["outline"] = ref;
    ref.material_template = std::make_shared<AssetRef>(main_pass);
    library_asset->material_bundle["main"] = ref;
    return library_asset;
}

std::pair <std::unique_ptr<Engine::RenderTargetTexture>, std::unique_ptr<Engine::RenderTargetTexture>>
MakeRenderTargetTextures(Engine::RenderSystem & system, std::pair<uint32_t, uint32_t> dimension) {
    auto color = Engine::RenderTargetTexture::CreateUnique(
        system,
        Engine::RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = dimension.first, .height = dimension.second, .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = Engine::RenderTargetTexture::RTTFormat::R8G8B8A8UNorm
        },
        Engine::RenderTargetTexture::SamplerDesc{},
        "Color Attachment"
    );
    auto depth = Engine::RenderTargetTexture::CreateUnique(
        system,
        Engine::RenderTargetTexture::RenderTargetTextureDesc{
            .dimensions = 2,
            .width = dimension.first, .height = dimension.second, .depth = 1,
            .mipmap_levels = 1,
            .array_layers = 1,
            .format = Engine::RenderTargetTexture::RTTFormat::D32SFLOAT
        },
        Engine::RenderTargetTexture::SamplerDesc{},
        "Depth Attachment"
    );
    return std::make_pair(std::move(color), std::move(depth));
}

Engine::RenderGraph BuildRenderGraph(
    Engine::RenderSystem & system,
    Engine::RenderTargetTexture & color,
    Engine::RenderTargetTexture & depth,
    Engine::GUISystem * gui_system
){
    using namespace Engine;
    Engine::RenderGraphBuilder rgb{system};
    rgb.RegisterImageAccess(color);
    rgb.RegisterImageAccess(depth);

    using IAT = Engine::AccessHelper::ImageAccessType;
    rgb.UseImage(color, IAT::ColorAttachmentWrite);
    rgb.UseImage(depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPass(
        Engine::AttachmentUtils::AttachmentDescription{
            &color,
            nullptr,
            AttachmentUtils::LoadOperation::Clear,
            AttachmentUtils::StoreOperation::Store,
            AttachmentUtils::ColorClearValue{1.0f, 1.0f, 1.0f, 1.0f}
        },
        Engine::AttachmentUtils::AttachmentDescription{
            &depth,
            nullptr,
            AttachmentUtils::LoadOperation::Clear,
            AttachmentUtils::StoreOperation::Store,
            AttachmentUtils::DepthClearValue{1.0f, 0U}
        },
        [&system, &color, &depth] (Engine::GraphicsCommandBuffer & gcb) -> void {
            vk::Extent2D extent{
                color.GetTextureDescription().width,
                color.GetTextureDescription().height
            };
            vk::Rect2D scissor{{0, 0}, extent};
            gcb.SetupViewport(extent.width, extent.height, scissor);
            gcb.DrawRenderers("outline", system.GetRendererManager().FilterAndSortRenderers({}));
        },
        ""
    );
    rgb.UseImage(color, IAT::ColorAttachmentWrite);
    rgb.UseImage(depth, IAT::DepthAttachmentWrite);
    rgb.RecordRasterizerPass(
        Engine::AttachmentUtils::AttachmentDescription{
            &color,
            nullptr,
            AttachmentUtils::LoadOperation::Load,
            AttachmentUtils::StoreOperation::Store
        },
        Engine::AttachmentUtils::AttachmentDescription{
            &depth,
            nullptr,
            AttachmentUtils::LoadOperation::Load,
            AttachmentUtils::StoreOperation::Store
        },
        [&system, &color, &depth] (Engine::GraphicsCommandBuffer & gcb) -> void {
            vk::Extent2D extent{
                color.GetTextureDescription().width,
                color.GetTextureDescription().height
            };
            vk::Rect2D scissor{{0, 0}, extent};
            gcb.SetupViewport(extent.width, extent.height, scissor);
            gcb.DrawRenderers("main", system.GetRendererManager().FilterAndSortRenderers({}));
        },
        ""
    );

    if (gui_system) {
        rgb.UseImage(color, IAT::ColorAttachmentWrite);
        rgb.RecordRasterizerPassWithoutRT([&system, gui_system, &color](Engine::GraphicsCommandBuffer &gcb) {
            gui_system->DrawGUI(
                {&color,
                    nullptr,
                    AttachmentUtils::LoadOperation::Load,
                    AttachmentUtils::StoreOperation::Store},
                system.GetSwapchain().GetExtent(),
                gcb
            );
        });
    }

    return rgb.BuildRenderGraph();
}

#endif // TOON_SHADING_RENDERING
