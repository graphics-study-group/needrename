/*
 * Cascaded directional shadow map test.
 *
 * A large scale scene (200x200 ground, objects one to a hundred and twenty
 * world units away from the camera's orbit) is rendered with the engine's
 * cascaded shadow maps, while the light rotates through the straight down
 * orientation and the camera orbits the scene. The run asserts what a
 * frame-running program can check without reading pixels:
 *
 *   - every cascade matrix of every shadow-casting light is finite, and the
 *     light has exactly the cascades it was configured with;
 *   - translating the light's GameObject leaves the uploaded cascade matrices
 *     unchanged (the cascades follow the camera, not the light);
 *   - no Vulkan validation error is reported during the run.
 *
 * Remaining acceptance is visual; see the change's acceptance checklist.
 */
#include <SDL3/SDL.h>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>

#include "Asset/AssetManager/AssetManager.h"
#include "Core/Functional/SDLWindow.h"
#include "Framework/Component/RenderComponent/CameraComponent.h"
#include "Framework/Component/RenderComponent/LightComponent.h"
#include "Framework/Component/RenderComponent/ObjTestMeshComponent.h"
#include "Framework/Component/RenderComponent/StaticMeshComponent.h"
#include "Framework/Component/TransformComponent/TransformComponent.h"
#include "Framework/MainClass.h"
#include "Framework/Object/GameObject.h"
#include "Framework/Tools/ShadowCascadeResources.h"
#include "Framework/World/Scene.h"
#include "Framework/World/WorldSystem.h"
#include "Render/Asset/Material/MaterialAsset.h"
#include "Render/Asset/Material/MaterialTemplateAsset.h"
#include "Render/Asset/Mesh/PlaneMeshAsset.h"
#include "Render/Asset/Texture/Image2DTextureAsset.h"
#include "Render/FullRenderSystem.h"
#include "Render/Pipeline/CommandBuffer.h"
#include "Render/Pipeline/RenderGraph/RenderGraph.h"
#include "Render/Pipeline/RenderGraph/RenderGraphBuilder.h"
#include "Render/Pipeline/RenderGraph/RenderGraphPass.h"
#include "Render/RenderSystem/CameraManager.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Render/RenderSystem/SceneDataManager.h"
#include "Render/Resource/RenderTargetTexture.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>

#include "cmake_config.h"

using namespace Engine;

namespace {
    /// Frames the whole animation spans: one full light revolution and one full
    /// camera orbit.
    constexpr int64_t ANIMATION_FRAMES = 300;

    /// The light invariance check runs on these two frames, where the animation
    /// is frozen so that only the light's position differs between them.
    constexpr int64_t LIGHT_INVARIANCE_FIRST_FRAME = 100;
    constexpr int64_t LIGHT_INVARIANCE_SECOND_FRAME = 101;

    /// Validation layer errors reported during the run.
    std::atomic<uint32_t> g_validation_errors{0};

    /**
     * @brief Whether a log message reports a Vulkan validation problem.
     *
     * A message counts when it names a VUID or a validation error. The one
     * exception is glslang's non-semantic debug info, which makes spirv-val
     * reject some shader modules in debug builds for every test in the project
     * (stock tests report it too); it is a toolchain defect, not a rendering one.
     */
    bool IsValidationProblem(const char *message) {
        if (message == nullptr) return false;
        if (std::strstr(message, "NonSemantic.Shader.DebugInfo") != nullptr) return false;
        return std::strstr(message, "Validation Error") != nullptr || std::strstr(message, "VUID-") != nullptr;
    }

    /**
     * @brief Log callback that counts validation problems while forwarding every
     * message to SDL's default output.
     */
    void CountValidationErrors(void *userdata, int category, SDL_LogPriority priority, const char *message) {
        if (IsValidationProblem(message)) {
            g_validation_errors.fetch_add(1, std::memory_order_relaxed);
        }
        SDL_GetDefaultLogOutputFunction()(userdata, category, priority, message);
    }

    bool IsFinite(const glm::mat4 &matrix) {
        for (glm::length_t column = 0; column < 4; column++) {
            for (glm::length_t row = 0; row < 4; row++) {
                if (!std::isfinite(matrix[column][row])) return false;
            }
        }
        return true;
    }

    float MaxDifference(const glm::mat4 &lhs, const glm::mat4 &rhs) {
        float difference = 0.0f;
        for (glm::length_t column = 0; column < 4; column++) {
            for (glm::length_t row = 0; row < 4; row++) {
                difference = std::max(difference, std::abs(lhs[column][row] - rhs[column][row]));
            }
        }
        return difference;
    }

    /**
     * @brief Rotation that makes a transform look from `position` towards `target`.
     *
     * The engine's transforms point along their local +Y, with local +Z as up.
     */
    glm::quat LookAtRotation(glm::vec3 position, glm::vec3 target) {
        const glm::vec3 forward = glm::normalize(target - position);
        glm::vec3 right = glm::cross(forward, glm::vec3{0.0f, 0.0f, 1.0f});
        if (glm::length(right) < 1e-4f) right = glm::vec3{1.0f, 0.0f, 0.0f};
        right = glm::normalize(right);
        const glm::vec3 up = glm::cross(right, forward);
        return glm::quat_cast(glm::mat3{right, forward, up});
    }

    using CascadeArray = std::array<glm::mat4, RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES>;

    /**
     * @brief Assert that every cascade of every shadow-casting light is usable.
     */
    void CheckCascadeMatrices(const RenderSystemState::SceneDataManager &scene_data, int64_t frame) {
        const uint32_t light_count = scene_data.GetNumShadowCastingLights();
        assert(light_count == 1 && "The test scene has exactly one shadow-casting directional light.");
        for (uint32_t light = 0; light < light_count; light++) {
            const uint32_t cascade_count = scene_data.GetLightCascadeCount(light);
            assert(
                cascade_count == RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES
                && "The light was configured with four cascades."
            );
            for (uint32_t cascade = 0; cascade < cascade_count; cascade++) {
                const glm::mat4 matrix = scene_data.GetLightCascadeMatrix(light, cascade);
                if (!IsFinite(matrix)) {
                    SDL_LogError(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Frame %lld: cascade %u of light %u is not finite.",
                        static_cast<long long>(frame),
                        cascade,
                        light
                    );
                }
                assert(IsFinite(matrix) && "A cascade matrix must be finite.");
                assert(matrix != glm::mat4{0.0f} && "A cascade matrix must have been fitted.");
            }
        }
    }

    /**
     * @brief Read every cascade of the scene's single shadow-casting light.
     */
    CascadeArray ReadCascades(const RenderSystemState::SceneDataManager &scene_data) {
        CascadeArray cascades{};
        for (uint32_t cascade = 0; cascade < cascades.size(); cascade++) {
            cascades[cascade] = scene_data.GetLightCascadeMatrix(0, cascade);
        }
        return cascades;
    }
} // namespace

struct LowerPlaneMeshAsset : public PlaneMeshAsset {
    constexpr static std::array<float, 12> REPLACEMENT_POSITION = {
        1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 0.5f, -1.0f, 1.0f, 0.5f, -1.0f, -1.0f, 0.5f
    };

    LowerPlaneMeshAsset() : PlaneMeshAsset() {
        assert(this->m_submeshes[0].positions.buffer_size == REPLACEMENT_POSITION.size() * sizeof(float));
        std::memcpy(
            reinterpret_cast<std::byte *>(m_submeshes[0].m_vertex_attributes.data())
                + this->m_submeshes[0].positions.buffer_offset,
            reinterpret_cast<const std::byte *>(REPLACEMENT_POSITION.data()),
            this->m_submeshes[0].positions.buffer_size
        );
        float *nb{
            reinterpret_cast<float *>(m_submeshes[0].m_vertex_attributes.data() + m_submeshes[0].normal.buffer_offset)
        };
        float *ne{reinterpret_cast<float *>(
            m_submeshes[0].m_vertex_attributes.data() + m_submeshes[0].normal.buffer_offset
            + m_submeshes[0].normal.buffer_size
        )};
        for (float *i = nb; i < ne; i += 3) {
            *(i + 2) = -1.0f;
        };
    }
};

class ShadowMapMeshComponent : public ObjTestMeshComponent {
public:
    ShadowMapMeshComponent(const GameObject &parent) : ObjTestMeshComponent(parent) {
    }

    void LoadData(std::filesystem::path mesh_file_name) {
        this->LoadMesh(mesh_file_name);
    }
};

std::array<MaterialTemplateAsset *, 2> ConstructMaterialTemplate() {
    auto am = MainClass::GetInstance()->GetAssetManager();
    std::array<MaterialTemplateAsset *, 2> templates{
        am->CreateAsset<MaterialTemplateAsset>(), am->CreateAsset<MaterialTemplateAsset>()
    };

    auto adb = std::dynamic_pointer_cast<FileSystemDatabase>(MainClass::GetInstance()->GetAssetDatabase());

    auto shadow_map_vs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/shadowmap.vert.asset"});
    auto vs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/blinn_phong.vert.asset"});
    auto fs_ref = adb->GetNewAssetRef(AssetPath{"builtin://shaders/blinn_phong.frag.asset"});

    templates[0]->name = "Blinn-Phong Lit";
    templates[1]->name = "Shadow map pass";

    MaterialTemplateSinglePassProperties shadow_map_pass{}, lit_pass{};
    shadow_map_pass.shaders.shaders = std::vector{shadow_map_vs_ref};
    shadow_map_pass.attachments.depth = Rhi::ImageFormat::D32SFLOAT;
    // The lit shaders carry the bias themselves (a per-cascade normal offset), so
    // the rasterizer keeps only a slope scaled fallback and no constant offset.
    shadow_map_pass.rasterizer.depth_bias_constant = 0.0f;
    shadow_map_pass.rasterizer.depth_bias_slope = 1.5f;

    lit_pass.shaders.shaders = std::vector{vs_ref, fs_ref};
    lit_pass.attachments.color = std::vector{Rhi::ImageFormat::R8G8B8A8UNorm};
    lit_pass.attachments.color_blending = std::vector{PipelineProperties::ColorBlendingProperties{}};
    lit_pass.attachments.depth = Rhi::ImageFormat::D32SFLOAT;

    templates[0]->properties = lit_pass;
    templates[1]->properties = shadow_map_pass;

    return templates;
}

MaterialLibraryAsset *ConstructMaterialLibrary(std::array<MaterialTemplateAsset *, 2> &templates) {
    auto am = MainClass::GetInstance()->GetAssetManager();
    auto *lib = am->CreateAsset<MaterialLibraryAsset>();
    lib->m_name = "Blinn-Phong w. Shadowmap";
    MaterialLibraryAsset::MaterialTemplateReference ref;
    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(templates[0]);
    lib->material_bundle["Lit"] = ref;

    ref.expected_mesh_type = 0;
    ref.material_template = AssetRef(templates[1]);
    lib->material_bundle["Shadowmap"] = ref;
    return lib;
}

int main(int argc, char **argv) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetLogOutputFunction(CountValidationErrors, nullptr);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Cascaded Shadow Maps Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    auto rsys = cmc->GetRenderSystem();
    auto am = cmc->GetAssetManager();
    auto world = cmc->GetWorldSystem();
    auto *mi_mng = rsys->GetRenderResourceManager<RenderSystemState::MaterialInstanceManager>();

    auto test_template_assets = ConstructMaterialTemplate();
    auto test_library_asset = ConstructMaterialLibrary(test_template_assets);
    auto test_library_asset_ref = AssetRef(test_library_asset);

    auto &scene = world->GetMainSceneRef();

    // Blank textures the test materials sample.
    auto idesc = Rhi::ImageTexture::ImageTextureDesc{
        .dimensions = 2,
        .width = 16,
        .height = 16,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = Rhi::ImageTexture::ImageTextureDesc::ImageTextureFormat::R8G8B8A8UNorm,
        .is_cube_map = false
    };
    std::shared_ptr blank_color_red = Engine::Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), idesc, Rhi::Texture::SamplerDesc{}, "Blank color red"
    );
    std::shared_ptr blank_color_gray = Engine::Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), idesc, Rhi::Texture::SamplerDesc{}, "Blank color gray"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_red, {1.0f, 0.0f, 0.0f, 0.0f});
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_gray, {0.5f, 0.5f, 0.5f, 0.0f});

    // ── Ground: a 200x200 plane whose surface sits at z = 0 ──────────────────
    auto floor_mat_asset = am->CreateAsset<MaterialAsset>();
    floor_mat_asset->m_library = test_library_asset_ref;
    auto floor_mat_asset_ref = AssetRef(floor_mat_asset);

    auto &floor_go = scene.CreateGameObject();
    floor_go.m_name = "Ground";
    floor_go.GetTransformRef().SetScale({100.0f, 100.0f, 1.0f}).SetPosition({0.0f, 0.0f, -0.5f});
    auto floor_mesh_asset = am->CreateAsset<LowerPlaneMeshAsset>();
    auto floor_mesh_asset_ref = AssetRef(floor_mesh_asset);
    auto &floor_mesh_comp = scene.CreateComponent<StaticMeshComponent>(floor_go);
    floor_mesh_comp.m_mesh_asset = floor_mesh_asset_ref;
    floor_mesh_comp.m_material_assets.resize(1);
    floor_mesh_comp.m_material_assets[0] = floor_mat_asset_ref;
    {
        auto floor_handle = mi_mng->CreateOrReuseFromAsset(floor_mat_asset_ref.GetGUID());
        auto floor_inst = mi_mng->Resolve(floor_handle);
        floor_inst->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
        floor_inst->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
        floor_inst->AssignTexture("base_tex", blank_color_gray);
    }

    // ── Casters at roughly 1 / 5 / 20 / 60 / 120 units from the orbit ────────
    auto cube_mat_asset = am->CreateAsset<MaterialAsset>();
    cube_mat_asset->m_library = test_library_asset_ref;
    auto cube_mat_asset_ref = AssetRef(cube_mat_asset);
    {
        auto cube_handle = mi_mng->CreateOrReuseFromAsset(cube_mat_asset_ref.GetGUID());
        auto cube_inst = mi_mng->Resolve(cube_handle);
        cube_inst->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
        cube_inst->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
        cube_inst->AssignTexture("base_tex", blank_color_red);
    }

    auto load_cube = [&](const char *name, glm::vec3 position, bool cast_shadow) -> void {
        auto &go = scene.CreateGameObject();
        go.m_name = name;
        go.GetTransformRef().SetScale({0.5f, 0.5f, 0.5f}).SetPosition(position);
        auto &mesh_comp = scene.CreateComponent<ShadowMapMeshComponent>(go);
        mesh_comp.LoadData(std::filesystem::path{std::string(ENGINE_ASSETS_DIR) + "/meshes/cube.obj"});
        mesh_comp.m_material_assets.resize(1);
        mesh_comp.m_material_assets[0] = cube_mat_asset_ref;
        // Read when the renderer registers itself, so it has to be set before the
        // scene's command queue is flushed.
        mesh_comp.m_cast_shadow = cast_shadow;
    };

    for (const float distance : {1.0f, 5.0f, 20.0f, 60.0f, 120.0f}) {
        load_cube("Caster", {distance, 0.0f, 0.25f}, true);
    }
    // Hovers 0.05 world units above the ground, for the contact shadow check.
    load_cube("Hovering caster", {5.0f, 6.0f, 0.30f}, true);
    // Deliberately opted out: it must contribute no shadow while still being lit.
    load_cube("Opted out caster", {20.0f, 15.0f, 0.25f}, false);

    // ── Light: one full revolution, through the straight down orientation ───
    auto &light_go = scene.CreateGameObject();
    light_go.m_name = "Rotating directional light";
    auto &light_comp = scene.CreateComponent<LightComponent>(light_go);
    light_comp.m_type = LightType::Directional;
    light_comp.m_cast_shadow = true;
    light_comp.m_shadow_distance = 100.0f;
    light_comp.m_cascade_count = RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES;

    // ── Camera: orbits the scene and looks at its centre ────────────────────
    auto &camera_go = scene.CreateGameObject();
    camera_go.m_name = "Orbiting camera";
    auto &camera_comp = scene.CreateComponent<CameraComponent>(camera_go);
    camera_comp.m_camera->m_clipping_near = 1e-3f;
    camera_comp.m_camera->m_clipping_far = 1e3f;
    camera_comp.m_camera->set_aspect_ratio(1920.0 / 1080.0);
    world->SetActiveCamera(camera_comp.GetHandle(), &rsys->GetCameraManager());
    rsys->GetCameraManager().SetActiveCameraIndex(camera_comp.m_camera->m_display_id);

    // Let the scene create and register everything. Renderers register themselves
    // in Awake, so this must run before the shadow map storage is sized and
    // before the frame loop starts.
    scene.FlushCmdQueue();
    scene.ProcessEvents();

    // One shadow map per shadow-casting light present; the shared helper queries
    // the world, so the light has to exist by now.
    assert(world->GetShadowCastingLightCount() == 1);

    // The shadow pass draws only renderers that opted in. The ground and the
    // hovering caster were created without touching the flag, so they prove the
    // default still casts; the deliberately opted out caster proves the flag is
    // honoured.
    const auto all_renderers = rsys->GetRendererManager().FilterAndSortRenderers({});
    const auto shadow_casters = rsys->GetRendererManager().FilterAndSortRenderers(
        {RenderSystemState::RendererManager::FilterCriteria::BinaryCriterion::Yes}
    );
    assert(all_renderers.size() == 8 && "Ground, five casters, one hovering and one opted out renderer.");
    assert(shadow_casters.size() == 7 && "The opted out renderer must not be drawn into shadow maps.");

    // Out-of-range configuration is clamped where it is consumed, so a stale or
    // hand-edited value can never reach the fit.
    {
        auto &scene_data = rsys->GetSceneDataManager();
        light_comp.m_cascade_count = 9;
        world->UpdateRendererData(*rsys);
        assert(scene_data.GetLightCascadeCount(0) == RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES);
        light_comp.m_cascade_count = 0;
        world->UpdateRendererData(*rsys);
        assert(scene_data.GetLightCascadeCount(0) == 1);
        light_comp.m_shadow_distance = 0.0f;
        world->UpdateRendererData(*rsys);
        assert(IsFinite(scene_data.GetLightCascadeMatrix(0, 0)) && "A rejected fit keeps usable cascades.");
        light_comp.m_cascade_count = RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES;
        light_comp.m_shadow_distance = 100.0f;
        world->UpdateRendererData(*rsys);
        assert(scene_data.GetLightCascadeCount(0) == RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES);

        // The budget accepts exactly the maximum, and an out-of-range light index
        // is rejected instead of writing past the light arrays.
        constexpr uint32_t MAX_LIGHTS = RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS;
        scene_data.SetLightCount(MAX_LIGHTS + 1);
        assert(scene_data.GetNumShadowCastingLights() == MAX_LIGHTS);
        scene_data.SetLightDirectional(MAX_LIGHTS, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f});
        assert(scene_data.GetLightCascadeCount(MAX_LIGHTS - 1) == 0 && "The rejected index must not be written.");
        scene_data.SetLightCount(1);
    }

    // ── Render graph ────────────────────────────────────────────────────────
    RenderGraphBuilder rgb{*rsys};

    const RGBufferHandle mm_handle = rgb.ImportExternalResource(
        rsys->GetSceneDataManager().GetModelMatricesBuffer(),
        Rhi::MemoryAccessTypeBuffer(Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite)
    );

    RenderTargetTexture::RenderTargetTextureDesc desc{
        .dimensions = 2,
        .width = 1920,
        .height = 1080,
        .depth = 1,
        .mipmap_levels = 1,
        .array_layers = 1,
        .format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::R8G8B8A8UNorm,
        .multisample = 1,
        .is_cube_map = false
    };
    auto c = rgb.RequestRenderTargetTexture(desc, {});
    desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
    auto d = rgb.RequestRenderTargetTexture(desc, {});

    // Shared with the application render graphs: one array texture per
    // shadow-casting light, and one pass rendering all of its cascades.
    const std::vector<RGTextureHandle> shadow_ids = AddShadowCascadePasses(rgb, *rsys, mm_handle);

    using IAT = Rhi::MemoryAccessTypeImageBits;

    {
        auto lit_builder = RenderGraphPassBuilder{*rsys}.SetName("Lit Pass");
        for (auto shadow_id : shadow_ids) {
            lit_builder.UseImage(shadow_id, IAT::ShaderSampledRead);
        }
        lit_builder
            .AppendColorAttachment(
                {c, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
            )
            .SetDepthStencilAttachment(
                {d,
                 {},
                 AttachmentUtils::LoadOperation::Clear,
                 AttachmentUtils::StoreOperation::DontCare,
                 AttachmentUtils::DepthClearValue{1.0f, 0U}}
            )
            .SetPassFunction([rsys, world](CommandBuffer &cb, const RenderGraph &) {
                auto active_camera = world->GetActiveCamera();
                if (!active_camera) return;
                rsys->GetCameraManager().SetActiveCameraIndex(active_camera->m_display_id);
                cb.DrawRenderers(
                    "Lit",
                    rsys->GetRendererManager().FilterAndSortRenderers({}),
                    active_camera->m_display_id,
                    rsys->GetPresentProvider().GetExtent()
                );
            })
            .WrapRenderPass();
        rgb.AddPass(lit_builder.Get());
    }

    auto rg{rgb.BuildRenderGraph()};
    RegisterShadowCascadeMaps(*rg, *rsys, shadow_ids);

    // ── Frame loop ──────────────────────────────────────────────────────────
    bool quited = false;
    int64_t frame_count = 0;
    CascadeArray saved_cascades{};

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_EVENT_QUIT) quited = true;
        }
        if (quited) break;

        ++frame_count;

        // Freeze the animation around the light invariance check, so that only
        // the light's position differs between those two frames.
        const bool frozen = frame_count == LIGHT_INVARIANCE_FIRST_FRAME || frame_count == LIGHT_INVARIANCE_SECOND_FRAME;
        const float phase = frozen ? 0.7f
                                   : 2.0f * std::numbers::pi_v<float>
                                         * static_cast<float>(frame_count) / static_cast<float>(ANIMATION_FRAMES);

        // The light makes one revolution about the world X axis per animation
        // span, which sweeps it through straight down, straight up and the
        // orientations the light basis would degenerate at.
        const float light_height = frozen ? 25.0f : 25.0f + 5.0f * std::sin(phase);
        light_go.GetTransformRef()
            .SetRotation(glm::angleAxis(phase, glm::vec3{1.0f, 0.0f, 0.0f}))
            .SetPosition({0.0f, 0.0f, light_height});

        // The camera makes one orbit around the world origin per animation span.
        const glm::vec3 camera_position{20.0f * std::cos(phase), 20.0f * std::sin(phase), 10.0f};
        camera_go.GetTransformRef()
            .SetPosition(camera_position)
            .SetRotation(LookAtRotation(camera_position, glm::vec3{0.0f, 0.0f, 0.0f}));
        camera_comp.Tick();

        world->UpdateRendererData(*rsys);
        CheckCascadeMatrices(rsys->GetSceneDataManager(), frame_count);

        if (frame_count == LIGHT_INVARIANCE_FIRST_FRAME) {
            saved_cascades = ReadCascades(rsys->GetSceneDataManager());
        } else if (frame_count == LIGHT_INVARIANCE_SECOND_FRAME) {
            // Translating the light must not change a single cascade matrix.
            light_go.GetTransformRef().SetPosition({37.0f, -12.0f, 3.0f});
            world->UpdateRendererData(*rsys);
            const CascadeArray moved_cascades = ReadCascades(rsys->GetSceneDataManager());
            for (uint32_t cascade = 0; cascade < saved_cascades.size(); cascade++) {
                const float difference = MaxDifference(saved_cascades[cascade], moved_cascades[cascade]);
                if (difference > 1e-6f) {
                    SDL_LogError(
                        SDL_LOG_CATEGORY_APPLICATION,
                        "Translating the light changed cascade %u by %f.",
                        cascade,
                        difference
                    );
                }
                assert(difference <= 1e-6f && "Translating the light must not change the cascades.");
            }
        }

        auto index = rsys->StartFrame();
        if (index == std::numeric_limits<uint32_t>::max()) {
            // Swapchain out of date after retry — skip this frame.
            continue;
        }
        assert(index < rsys->GetPresentProvider().GetImageCount());

        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);
        auto color = rg->GetInternalTextureResource(c);
        rsys->CompleteFrame(*color, Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite);

        SDL_Delay(10);

        if (frame_count >= max_frame_count) break;
    }

    rsys->WaitForIdle();

    const uint32_t validation_errors = g_validation_errors.load(std::memory_order_relaxed);
    if (validation_errors != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The run reported %u validation layer errors.", validation_errors);
        return 1;
    }
    return 0;
}
