/*
 * Cascaded directional shadow map test.
 *
 * A large scale scene (200x200 ground, objects one to a hundred and twenty
 * world units away from the camera's orbit) is rendered with the engine's
 * cascaded shadow maps, while the light sweeps from straight down to almost
 * horizontal and the camera orbits the scene. The run asserts what a
 * frame-running program can check without reading pixels:
 *
 *   - every cascade matrix of every shadow-casting light is finite, and the
 *     light has exactly the cascades it was configured with;
 *   - translating the light's GameObject leaves the uploaded cascade matrices
 *     unchanged (the cascades follow the camera, not the light);
 *   - the render-owned shadow storage exists for every budgeted slot and is
 *     never replaced;
 *   - handing the shadow-caster role to another light at runtime starts or stops
 *     shadows on the next frame with no render-graph rebuild, and a role holder
 *     that is removed is taken over by the remaining flagged light;
 *   - destroying and rebuilding the render graph mid-run keeps rendering shadows
 *     from the same storage;
 *   - no Vulkan validation error is reported during the run.
 *
 * Remaining acceptance is visual; see the change's acceptance checklist.
 *
 * Run it with a frame count to get exactly that: the light and the camera are
 * animated from the frame counter and the program exits on its own. Run it
 * without arguments to get an interactive session instead, where a control
 * window drives the light's orientation, the light settings that feed the
 * cascade fit and the camera placement, and the same per frame checks keep
 * running so the window cannot hide a regression.
 */
#include <Core/FileSystem/DataRoot.h>
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
#include "Render/UserInterface/GUISystem.h"
#include <Asset/AssetDatabase/FileSystemDatabase.h>
#include <imgui.h>

#include "cmake_config.h"

using namespace Engine;

namespace {
    /// Frames the whole animation spans: one full light sweep and one full
    /// camera orbit.
    constexpr int64_t ANIMATION_FRAMES = 300;

    /// How far the light leans away from straight down at the peak of the
    /// sweep, in degrees. Past 81.9 degrees the light direction is close enough
    /// to the world Y axis for the shadow basis to switch its reference up axis
    /// from Y to Z, and staying below 90 degrees keeps the sun above the horizon
    /// so the run never renders a legitimately black scene.
    constexpr float MAX_LIGHT_TILT_DEGREES = 85.0f;

    /// The light invariance check runs on these two frames, where the animation
    /// is frozen so that only the light's position differs between them.
    constexpr int64_t LIGHT_INVARIANCE_FIRST_FRAME = 100;
    constexpr int64_t LIGHT_INVARIANCE_SECOND_FRAME = 101;

    /// The automatic mode hands the shadow-caster role to the standby lights
    /// around these frames, so adding and removing lights at runtime is
    /// exercised while frames keep rendering.
    constexpr int64_t RUNTIME_TAKEOVER_FRAME = 40;
    constexpr int64_t RUNTIME_SECOND_FLAG_FRAME = 41;
    constexpr int64_t RUNTIME_REMOVE_HOLDER_FRAME = 42;
    constexpr int64_t RUNTIME_RESTORE_FRAME = 44;

    /// The graph is destroyed and rebuilt here, mid-run, to prove that the shadow
    /// storage survives a rebuild untouched.
    constexpr int64_t GRAPH_REBUILD_FRAME = 60;

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
     *
     * @param scene_data          Scene data to inspect.
     * @param frame               Frame counter, used for the failure message.
     * @param expect_all_cascades Whether the light is expected to keep every
     *                            cascade. The automatic mode animates a light that
     *                            is always configured with four; the interactive
     *                            mode lets the count be edited, so it only checks
     *                            that the fitted count stays in the supported
     *                            range and that each fitted cascade is usable.
     */
    void CheckCascadeMatrices(
        const RenderSystemState::SceneDataManager &scene_data, int64_t frame, bool expect_all_cascades
    ) {
        const uint32_t light_count = scene_data.GetNumShadowCastingLights();
        assert(light_count == 1 && "The test scene has exactly one shadow-casting directional light.");
        for (uint32_t light = 0; light < light_count; light++) {
            const uint32_t cascade_count = scene_data.GetLightCascadeCount(light);
            assert(
                cascade_count >= 1 && cascade_count <= RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES
                && "A shadow casting light keeps between one and MAX_SHADOW_CASCADES cascades."
            );
            assert(!expect_all_cascades || cascade_count == RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES);
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

    /**
     * @brief Light and camera placement the interactive mode exposes.
     *
     * Only the interactive mode (started without a frame count) writes this; a
     * run with a frame count drives the very same objects from the frame counter
     * so that the automatic checks stay reproducible.
     */
    struct InteractiveState {
        /// How far the light leans away from straight down, in degrees. Kept
        /// below 90 so the sun stays above the horizon.
        float light_tilt_degrees{35.0f};
        /// Rotation of the light around the world Z axis, in degrees.
        float light_azimuth_degrees{40.0f};
        /// Colour scaled intensity handed to the light component.
        float light_intensity{1.0f};
        /// Distance the light's shadows reach.
        float shadow_distance{100.0f};
        /// Number of cascades the light is configured with.
        int cascade_count{static_cast<int>(RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES)};
        /// Whether the camera is placed by its position instead of the orbit.
        bool free_camera{false};
        float camera_azimuth_degrees{60.0f};
        /// Far enough back that every primitive in the scene fits the view.
        float camera_orbit_radius{26.0f};
        float camera_orbit_height{13.0f};
        /// Used while `free_camera` is set; defaults to the orbit's first pose.
        glm::vec3 camera_position{13.0f, 22.516661f, 13.0f};
    };

    /**
     * @brief World position of the camera the interactive state describes.
     */
    glm::vec3 InteractiveCameraPosition(const InteractiveState &state) {
        if (state.free_camera) return state.camera_position;
        const float azimuth = glm::radians(state.camera_azimuth_degrees);
        return {
            state.camera_orbit_radius * std::cos(azimuth),
            state.camera_orbit_radius * std::sin(azimuth),
            state.camera_orbit_height
        };
    }

    /**
     * @brief Rotation whose local +Y is the light's direction of travel.
     */
    glm::quat InteractiveLightRotation(const InteractiveState &state) {
        return glm::angleAxis(glm::radians(state.light_azimuth_degrees), glm::vec3{0.0f, 0.0f, 1.0f})
               * glm::angleAxis(
                   glm::radians(state.light_tilt_degrees) - glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}
               );
    }

    /**
     * @brief Build the interactive mode's control window.
     *
     * Exposes the light's orientation, the light settings that reach the cascade
     * fit and the camera placement, so the cascade layout and the shadow quality
     * can be inspected by hand. The fitted cascades are listed with the world
     * width the shading derives from their matrices, which is the number that
     * says how dense each cascade's texels are.
     */
    void BuildControlWindow(InteractiveState &state, const RenderSystemState::SceneDataManager &scene_data) {
        ImGui::Begin("Cascaded Shadow Maps Test", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        // Fix the widget width so the labels next to the sliders keep their room
        // whatever the window ends up being wide.
        ImGui::PushItemWidth(190.0f);

        ImGui::TextWrapped("Interactive mode: no frame limit. Pass a frame count to run the checks only.");
        if (ImGui::Button("Reset view")) state = InteractiveState{};

        ImGui::SeparatorText("Light");
        ImGui::SliderFloat("Tilt (deg)", &state.light_tilt_degrees, 0.0f, 89.0f, "%.1f");
        ImGui::SliderFloat("Azimuth (deg)", &state.light_azimuth_degrees, 0.0f, 360.0f, "%.1f");
        ImGui::SliderFloat("Intensity", &state.light_intensity, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Shadow distance", &state.shadow_distance, 1.0f, 300.0f, "%.1f");
        ImGui::SliderInt(
            "Cascades",
            &state.cascade_count,
            1,
            static_cast<int>(RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES)
        );

        ImGui::SeparatorText("Camera");
        ImGui::Checkbox("Free position", &state.free_camera);
        if (state.free_camera) {
            ImGui::SliderFloat("X", &state.camera_position.x, -150.0f, 150.0f, "%.1f");
            ImGui::SliderFloat("Y", &state.camera_position.y, -150.0f, 150.0f, "%.1f");
            ImGui::SliderFloat("Z", &state.camera_position.z, -20.0f, 150.0f, "%.1f");
        } else {
            ImGui::SliderFloat("Orbit azimuth (deg)", &state.camera_azimuth_degrees, 0.0f, 360.0f, "%.1f");
            ImGui::SliderFloat("Orbit radius", &state.camera_orbit_radius, 1.0f, 150.0f, "%.1f");
            ImGui::SliderFloat("Orbit height", &state.camera_orbit_height, 0.1f, 150.0f, "%.1f");
        }
        const glm::vec3 camera_position = InteractiveCameraPosition(state);
        ImGui::Text(
            "Camera at (%.2f, %.2f, %.2f), looking at the origin",
            camera_position.x,
            camera_position.y,
            camera_position.z
        );

        ImGui::SeparatorText("Fitted cascades");
        const uint32_t light_count = scene_data.GetNumShadowCastingLights();
        for (uint32_t light = 0; light < light_count; light++) {
            const uint32_t cascade_count = scene_data.GetLightCascadeCount(light);
            ImGui::Text("Light %u: %u cascade(s)", light, cascade_count);
            for (uint32_t cascade = 0; cascade < cascade_count; cascade++) {
                const glm::mat4 matrix = scene_data.GetLightCascadeMatrix(light, cascade);
                const float scale = glm::length(glm::vec3{matrix[0][0], matrix[1][0], matrix[2][0]});
                const float width = scale > 0.0f ? 2.0f / scale : 0.0f;
                ImGui::Text(
                    "  %u: %.2f units wide, %.4f per texel",
                    cascade,
                    width,
                    width / static_cast<float>(RenderSystemState::SceneDataManager::SHADOWMAP_SIZE)
                );
            }
        }

        ImGui::PopItemWidth();
        ImGui::End();
    }

    /**
     * @brief Fill a mesh asset with a cylinder of radius 1 and height 2.
     *
     * The builtin assets ship a cube and a sphere but no cylinder, and the scene
     * wants every primitive shape to be shadow checked. The size matches the
     * loaded meshes (two units across), the axis is world Z and the caps are
     * closed, so the cylinder both shadows itself and casts from both ends.
     */
    void BuildCylinderMesh(MeshAsset &mesh) {
        constexpr uint32_t SEGMENTS = 32;
        constexpr float RADIUS = 1.0f;
        constexpr float HALF_HEIGHT = 1.0f;

        std::vector<float> positions{};
        std::vector<float> normals{};
        std::vector<float> texcoords{};
        std::vector<uint32_t> indices{};

        const float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(SEGMENTS);
        // Side wall: two rings of vertices with radial normals.
        for (uint32_t segment = 0; segment < SEGMENTS; segment++) {
            const float angle = step * static_cast<float>(segment);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            for (const float z : {-HALF_HEIGHT, HALF_HEIGHT}) {
                positions.insert(positions.end(), {cosine * RADIUS, sine * RADIUS, z});
                normals.insert(normals.end(), {cosine, sine, 0.0f});
                texcoords.insert(
                    texcoords.end(),
                    {static_cast<float>(segment) / static_cast<float>(SEGMENTS), (z + HALF_HEIGHT) * 0.5f}
                );
            }
        }
        for (uint32_t segment = 0; segment < SEGMENTS; segment++) {
            const uint32_t current = segment * 2;
            const uint32_t next = ((segment + 1) % SEGMENTS) * 2;
            indices.insert(indices.end(), {current, next, current + 1, next, next + 1, current + 1});
        }
        // Caps: a centre vertex plus a ring, with flat normals along the axis.
        for (const float z : {-HALF_HEIGHT, HALF_HEIGHT}) {
            const float side = z < 0.0f ? -1.0f : 1.0f;
            const uint32_t centre = static_cast<uint32_t>(positions.size() / 3);
            positions.insert(positions.end(), {0.0f, 0.0f, z});
            normals.insert(normals.end(), {0.0f, 0.0f, side});
            texcoords.insert(texcoords.end(), {0.5f, 0.5f});
            const uint32_t ring = static_cast<uint32_t>(positions.size() / 3);
            for (uint32_t segment = 0; segment < SEGMENTS; segment++) {
                const float angle = step * static_cast<float>(segment);
                positions.insert(positions.end(), {std::cos(angle) * RADIUS, std::sin(angle) * RADIUS, z});
                normals.insert(normals.end(), {0.0f, 0.0f, side});
                texcoords.insert(texcoords.end(), {std::cos(angle) * 0.5f + 0.5f, std::sin(angle) * 0.5f + 0.5f});
            }
            for (uint32_t segment = 0; segment < SEGMENTS; segment++) {
                const uint32_t current = ring + segment;
                const uint32_t next = ring + (segment + 1) % SEGMENTS;
                if (side < 0.0f) {
                    indices.insert(indices.end(), {centre, next, current});
                } else {
                    indices.insert(indices.end(), {centre, current, next});
                }
            }
        }

        // The attribute array is laid out per attribute, the way the other
        // builtin meshes store it.
        const size_t vertex_count = positions.size() / 3;
        const size_t position_bytes = positions.size() * sizeof(float);
        const size_t texcoord_bytes = texcoords.size() * sizeof(float);
        mesh.m_name = "Cylinder";
        mesh.m_submeshes.resize(1);
        auto &submesh = mesh.m_submeshes[0];
        submesh.m_indices = std::move(indices);
        submesh.vertex_count = static_cast<uint32_t>(vertex_count);
        submesh.positions = {VertexAttributeType::SFloat32x3, 0, position_bytes};
        submesh.color = {VertexAttributeType::SFloat32x3, position_bytes, position_bytes};
        submesh.normal = {VertexAttributeType::SFloat32x3, position_bytes * 2, position_bytes};
        submesh.texcoord0 = {VertexAttributeType::SFloat32x2, position_bytes * 3, texcoord_bytes};

        submesh.m_vertex_attributes.resize(position_bytes * 3 + texcoord_bytes);
        std::byte *cursor = submesh.m_vertex_attributes.data();
        std::memcpy(cursor, positions.data(), position_bytes);
        cursor += position_bytes;
        // Vertex colour stays white: the material's texture provides the colour.
        const std::vector<float> colors(vertex_count * 3, 1.0f);
        std::memcpy(cursor, colors.data(), position_bytes);
        cursor += position_bytes;
        std::memcpy(cursor, normals.data(), position_bytes);
        cursor += position_bytes;
        std::memcpy(cursor, texcoords.data(), texcoord_bytes);
    }
} // namespace

/**
 * @brief Ground plane whose surface sits at the parent transform's origin.
 *
 * `PlaneMeshAsset` spends half of its unit extent along Z, so the four corners
 * are pushed to +0.5 to put the surface on the transform's own plane. The
 * normals keep pointing at +Z, the side the orbiting camera looks at, so the
 * ground is lit by the light while it is above the horizon.
 */
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
    // A frame count selects the automatic mode: the light and the camera are
    // driven by the frame counter and the run stops after that many frames.
    // Without one the run is interactive and exposes the same objects through a
    // control window instead.
    const bool interactive = argc <= 1;
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (!interactive) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);
    SDL_SetLogOutputFunction(CountValidationErrors, nullptr);

    StartupOptions opt{.resol_x = 1920, .resol_y = 1080, .title = "Cascaded Shadow Maps Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(BuiltinAssetsDir());

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
    std::shared_ptr blank_color_blue = Engine::Rhi::ImageTexture::CreateUnique(
        rsys->GetDeviceContext(), idesc, Rhi::Texture::SamplerDesc{}, "Blank color blue"
    );
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_red, {1.0f, 0.0f, 0.0f, 0.0f});
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_gray, {0.5f, 0.5f, 0.5f, 0.0f});
    rsys->GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*blank_color_blue, {0.1f, 0.35f, 1.0f, 0.0f});

    // Ground: a 200x200 plane whose surface sits at z = 0.
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

    // Casters at roughly 1 / 5 / 20 / 60 / 120 units from the orbit.
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

    // Renderers that opted out of shadow casting use their own colour, so the
    // picture says which objects cast without reading the flag.
    auto non_casting_mat_asset = am->CreateAsset<MaterialAsset>();
    non_casting_mat_asset->m_library = test_library_asset_ref;
    auto non_casting_mat_asset_ref = AssetRef(non_casting_mat_asset);
    {
        auto handle = mi_mng->CreateOrReuseFromAsset(non_casting_mat_asset_ref.GetGUID());
        auto instance = mi_mng->Resolve(handle);
        instance->AssignVectorVariable("ambient_color", glm::vec4(0.0, 0.0, 0.0, 0.0));
        instance->AssignVectorVariable("specular_color", glm::vec4(1.0, 1.0, 1.0, 64.0));
        instance->AssignTexture("base_tex", blank_color_blue);
    }

    // Every primitive asset spans two world units, so a 0.5 scale makes a one
    // unit tall object that rests on the ground at z = 0.5.
    constexpr float PRIMITIVE_SCALE = 0.5f;
    constexpr float RESTING_HEIGHT = 0.5f;

    auto load_mesh = [&](const char *name, const char *mesh_file, glm::vec3 position, bool cast_shadow) -> void {
        auto &go = scene.CreateGameObject();
        go.m_name = name;
        go.GetTransformRef().SetScale(glm::vec3{PRIMITIVE_SCALE}).SetPosition(position);
        auto &mesh_comp = scene.CreateComponent<ShadowMapMeshComponent>(go);
        mesh_comp.LoadData(std::filesystem::path{(AssetsDir() / "meshes" / mesh_file).string()});
        mesh_comp.m_material_assets.resize(1);
        mesh_comp.m_material_assets[0] = cast_shadow ? cube_mat_asset_ref : non_casting_mat_asset_ref;
        // Read when the renderer registers itself, so it has to be set before the
        // scene's command queue is flushed.
        mesh_comp.m_cast_shadow = cast_shadow;
    };

    // The cylinder is built here: the builtin assets ship a cube and a sphere but
    // no cylinder, and the scene wants every primitive to be shape checked.
    auto cylinder_mesh = am->CreateAsset<MeshAsset>();
    BuildCylinderMesh(*cylinder_mesh);
    auto cylinder_mesh_ref = AssetRef(cylinder_mesh);

    auto load_cylinder = [&](const char *name, glm::vec3 position, bool cast_shadow) -> void {
        auto &go = scene.CreateGameObject();
        go.m_name = name;
        go.GetTransformRef().SetScale(glm::vec3{PRIMITIVE_SCALE}).SetPosition(position);
        auto &mesh_comp = scene.CreateComponent<StaticMeshComponent>(go);
        mesh_comp.m_mesh_asset = cylinder_mesh_ref;
        mesh_comp.m_material_assets.resize(1);
        mesh_comp.m_material_assets[0] = cast_shadow ? cube_mat_asset_ref : non_casting_mat_asset_ref;
        mesh_comp.m_cast_shadow = cast_shadow;
    };

    for (const float distance : {1.0f, 5.0f, 20.0f, 60.0f, 120.0f}) {
        load_mesh("Caster", "cube.obj", {distance, 0.0f, RESTING_HEIGHT}, true);
    }
    // Hovers 0.05 world units above the ground, for the contact shadow check.
    load_mesh("Hovering caster", "cube.obj", {5.0f, 6.0f, RESTING_HEIGHT + 0.05f}, true);
    // Deliberately opted out: they must contribute no shadow while still being lit.
    load_mesh("Opted out cube", "cube.obj", {8.0f, -2.0f, RESTING_HEIGHT}, false);
    load_mesh("Opted out sphere", "sphere.obj", {-9.0f, -2.0f, RESTING_HEIGHT}, false);

    load_mesh("Sphere caster", "sphere.obj", {-6.0f, 2.0f, RESTING_HEIGHT}, true);
    load_mesh("Sphere caster", "sphere.obj", {3.0f, -6.0f, RESTING_HEIGHT}, true);

    load_cylinder("Cylinder caster", {9.0f, 2.0f, RESTING_HEIGHT}, true);
    load_cylinder("Cylinder caster", {-5.0f, -8.0f, RESTING_HEIGHT}, true);

    // Light: sweeps from straight down to almost horizontal.
    auto &light_go = scene.CreateGameObject();
    light_go.m_name = "Rotating directional light";
    auto &light_comp = scene.CreateComponent<LightComponent>(light_go);
    light_comp.m_type = LightType::Directional;
    light_comp.m_cast_shadow = true;
    // The default intensity saturates the test's 0.5 grey ground into flat
    // white, which would hide the shadow pattern the run is meant to show.
    light_comp.m_intensity = 1.0f;
    light_comp.m_shadow_distance = 100.0f;
    light_comp.m_cascade_count = RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES;

    // Standby lights the runtime role tests hand the role to. They are created
    // after the animated light (so the animated one wins the role by creation
    // order whenever both are flagged) and start unflagged; only the tests below
    // ever set the flag. Their pose is fixed so a hand-over produces a valid fit.
    auto create_standby_light = [&](const char *name) -> LightComponent & {
        auto &go = scene.CreateGameObject();
        go.m_name = name;
        go.GetTransformRef()
            .SetRotation(glm::angleAxis(glm::radians(-55.0f), glm::vec3{1.0f, 0.0f, 0.0f}))
            .SetPosition({0.0f, 0.0f, 25.0f});
        auto &comp = scene.CreateComponent<LightComponent>(go);
        comp.m_type = LightType::Directional;
        comp.m_cast_shadow = false;
        comp.m_intensity = 1.0f;
        comp.m_shadow_distance = 100.0f;
        comp.m_cascade_count = RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES;
        return comp;
    };
    auto &standby_light_a = create_standby_light("Standby shadow light A");
    auto &standby_light_b = create_standby_light("Standby shadow light B");

    // Camera: orbits the scene and looks at its centre.
    auto &camera_go = scene.CreateGameObject();
    camera_go.m_name = "Orbiting camera";
    auto &camera_comp = scene.CreateComponent<CameraComponent>(camera_go);
    camera_comp.m_camera->m_clipping_near = 1e-3f;
    camera_comp.m_camera->m_clipping_far = 1e3f;
    camera_comp.m_camera->set_aspect_ratio(1920.0f / 1080.0f);
    world->SetActiveCamera(camera_comp.GetHandle(), &rsys->GetCameraManager());
    rsys->GetCameraManager().SetActiveCameraIndex(camera_comp.m_camera->m_display_id);

    // Let the scene create and register everything. Renderers register themselves
    // in Awake, so this must run before the frame loop starts.
    scene.FlushCmdQueue();
    scene.ProcessEvents();

    // The shadow storage is allocated with the render system, so it exists no
    // matter which lights the scene has; the scene just has one role holder.
    assert(world->GetShadowCastingLight() == &light_comp && "The only flagged light holds the shadow-caster role.");
    // Every budgeted slot has its own storage from the moment the render system
    // exists, at the resolution and layer count the shaders expect.
    for (uint32_t slot = 0; slot < RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS; slot++) {
        const auto &storage = rsys->GetSceneDataManager().GetShadowMapTexture(slot).GetTextureDescription();
        assert(storage.format == Rhi::ImageFormat::D32SFLOAT);
        assert(storage.width == RenderSystemState::SceneDataManager::SHADOWMAP_SIZE);
        assert(storage.height == RenderSystemState::SceneDataManager::SHADOWMAP_SIZE);
        assert(storage.array_layers == RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES);
    }
    // The storage object is never replaced, which is what lets graphs import it
    // once and what the runtime hand-over and rebuild checks below rely on.
    auto *const shadow_texture = &rsys->GetSceneDataManager().GetShadowMapTexture(0);

    // The shadow pass draws only renderers that opted in. Everything except the
    // two blue primitives casts, and the ones created without touching the flag
    // prove the default still casts.
    const auto all_renderers = rsys->GetRendererManager().FilterAndSortRenderers({});
    const auto shadow_casters = rsys->GetRendererManager().FilterAndSortRenderers(
        {RenderSystemState::RendererManager::FilterCriteria::BinaryCriterion::Yes}
    );
    assert(all_renderers.size() == 13 && "Ground, seven cubes, three spheres and two cylinders.");
    assert(shadow_casters.size() == 11 && "The two opted out renderers must not be drawn into shadow maps.");

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

        // The light budget accepts exactly the maximum, and an out-of-range light
        // index is rejected instead of writing past the light arrays.
        constexpr uint32_t MAX_LIGHTS = RenderSystemState::SceneDataManager::MAX_SHADOW_CASTING_LIGHTS;
        const uint32_t holder_cascades = scene_data.GetLightCascadeCount(0);
        scene_data.SetLightCount(MAX_LIGHTS + 1);
        assert(scene_data.GetNumShadowCastingLights() == MAX_LIGHTS);
        scene_data.SetLightDirectional(MAX_LIGHTS, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f});
        assert(scene_data.GetLightCascadeCount(0) == holder_cascades && "The rejected index must not be written.");
        scene_data.SetLightCount(1);
    }

    // Render graph.
    // The interactive mode composites a control window over the lit image, so it
    // needs the GUI backend; the automatic mode touches nothing of it.
    GUISystem *gui_system = nullptr;
    if (interactive) {
        gui_system = cmc->GetGUISystem().get();
        gui_system->CreateVulkanBackend(*rsys, Rhi::GetVkFormat(Rhi::ImageFormat::R8G8B8A8UNorm));
    }

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

    // The interactive mode draws the control window in a second pass over the
    // lit image. Two passes sharing one *transient* target are ordered by a
    // "read before write" heuristic that would put the compositing pass first,
    // so the interactive run owns the color texture and imports it instead: the
    // pass order of an external resource is kept as it is declared.
    std::unique_ptr<RenderTargetTexture> interactive_color{};
    if (interactive) {
        interactive_color = RenderTargetTexture::CreateUnique(rsys->GetDeviceContext(), desc, {}, "Lit Color");
    }

    // Building the graph is a function of the render system alone: the shadow
    // storage is render-owned, so the run can rebuild the graph mid-flight and
    // must keep rendering shadows with no re-registration step.
    RGTextureHandle color_handle{};
    auto build_render_graph = [&]() -> std::unique_ptr<RenderGraph> {
        RenderGraphBuilder rgb{*rsys};

        const RGBufferHandle mm_handle = rgb.ImportExternalResource(
            rsys->GetSceneDataManager().GetModelMatricesBuffer(),
            Rhi::MemoryAccessTypeBuffer(Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite)
        );

        auto rtt_desc = desc;
        if (interactive) {
            color_handle = rgb.ImportExternalResource(*interactive_color);
        } else {
            color_handle = rgb.RequestRenderTargetTexture(rtt_desc, {});
        }

        rtt_desc.format = RenderTargetTexture::RenderTargetTextureDesc::RTTFormat::D32SFLOAT;
        auto d = rgb.RequestRenderTargetTexture(rtt_desc, {});

        // Shared with the application render graphs: every budgeted slot is
        // borrowed, and one pass renders all of its light's cascades.
        const std::vector<RGTextureHandle> shadow_ids = AddShadowCascadePasses(rgb, *rsys, mm_handle);

        using IAT = Rhi::MemoryAccessTypeImageBits;

        {
            auto lit_builder = RenderGraphPassBuilder{*rsys}.SetName("Lit Pass");
            for (auto shadow_id : shadow_ids) {
                lit_builder.UseImage(shadow_id, IAT::ShaderSampledRead);
            }
            lit_builder
                .AppendColorAttachment(
                    {color_handle, {}, AttachmentUtils::LoadOperation::Clear, AttachmentUtils::StoreOperation::Store}
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

        if (interactive) {
            // Composite the control window over the lit image. `Load` keeps the lit
            // pass' contents; the GUI draws on top of them.
            rgb.AddPass(
                RenderGraphPassBuilder{*rsys}
                    .SetName("GUI Pass")
                    .AppendColorAttachment(
                        {color_handle, {}, AttachmentUtils::LoadOperation::Load, AttachmentUtils::StoreOperation::Store}
                    )
                    .SetPassFunction([gui_system](CommandBuffer &cb, const RenderGraph &) {
                        gui_system->DrawGUI(cb.GetCommandBuffer());
                    })
                    .WrapRenderPass()
                    .Get()
            );
        }

        return rgb.BuildRenderGraph();
    };

    std::unique_ptr<RenderGraph> rg = build_render_graph();

    // Frame loop.
    bool quited = false;
    int64_t frame_count = 0;
    CascadeArray saved_cascades{};
    InteractiveState interactive_state{};

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_EVENT_QUIT) quited = true;
            if (interactive) gui_system->ProcessEvent(&event);
        }
        if (quited) break;

        ++frame_count;

        if (interactive) {
            // The control window owns the light and the camera, so the objects
            // are driven from its state instead of the frame counter.
            light_comp.m_intensity = interactive_state.light_intensity;
            light_comp.m_shadow_distance = interactive_state.shadow_distance;
            light_comp.m_cascade_count = static_cast<uint32_t>(std::max(interactive_state.cascade_count, 1));
            light_go.GetTransformRef()
                .SetRotation(InteractiveLightRotation(interactive_state))
                .SetPosition({0.0f, 0.0f, 25.0f});

            // The camera always looks at the world origin, which has no valid
            // view rotation when the camera sits on it, so it is nudged off.
            glm::vec3 camera_position = InteractiveCameraPosition(interactive_state);
            if (glm::length(camera_position) < 1e-3f) camera_position.z = 1e-3f;
            camera_go.GetTransformRef()
                .SetPosition(camera_position)
                .SetRotation(LookAtRotation(camera_position, glm::vec3{0.0f, 0.0f, 0.0f}));
            camera_comp.Tick();
        } else {
            // Freeze the animation around the light invariance check, so that only
            // the light's position differs between those two frames.
            const bool frozen =
                frame_count == LIGHT_INVARIANCE_FIRST_FRAME || frame_count == LIGHT_INVARIANCE_SECOND_FRAME;
            const float phase = frozen ? 0.7f
                                       : 2.0f * std::numbers::pi_v<float>
                                             * static_cast<float>(frame_count) / static_cast<float>(ANIMATION_FRAMES);

            // The light sweeps from straight down to almost horizontal and back,
            // so the run covers the straight down orientation and the light
            // direction the basis switches its reference up axis for, while
            // staying above the horizon: a sun below the ground would
            // legitimately render the scene black and hide everything the test
            // is meant to show.
            const float light_tilt = glm::radians(MAX_LIGHT_TILT_DEGREES) * 0.5f * (1.0f - std::cos(phase));
            const float light_height = frozen ? 25.0f : 25.0f + 5.0f * std::sin(phase);
            light_go.GetTransformRef()
                .SetRotation(glm::angleAxis(light_tilt - glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f}))
                .SetPosition({0.0f, 0.0f, light_height});

            // The camera makes one orbit around the world origin per animation span.
            const glm::vec3 camera_position{20.0f * std::cos(phase), 20.0f * std::sin(phase), 10.0f};
            camera_go.GetTransformRef()
                .SetPosition(camera_position)
                .SetRotation(LookAtRotation(camera_position, glm::vec3{0.0f, 0.0f, 0.0f}));
            camera_comp.Tick();
        }

        if (!interactive) {
            // Runtime role changes and one mid-run graph rebuild. The shadow storage
            // is render-owned, so none of these may (re)allocate or re-register it.
            switch (frame_count) {
            case RUNTIME_TAKEOVER_FRAME:
                light_comp.m_cast_shadow = false;
                standby_light_a.m_cast_shadow = true;
                break;
            case RUNTIME_SECOND_FLAG_FRAME:
                standby_light_b.m_cast_shadow = true;
                break;
            case RUNTIME_REMOVE_HOLDER_FRAME:
                scene.RemoveGameObject(standby_light_a.GetParentGameObject()->GetHandle());
                scene.FlushCmdQueue();
                break;
            case RUNTIME_RESTORE_FRAME:
                scene.RemoveGameObject(standby_light_b.GetParentGameObject()->GetHandle());
                scene.FlushCmdQueue();
                light_comp.m_cast_shadow = true;
                break;
            case GRAPH_REBUILD_FRAME:
                rsys->WaitForIdle();
                rg = build_render_graph();
                break;
            default:
                break;
            }
        }

        world->UpdateRendererData(*rsys);
        CheckCascadeMatrices(rsys->GetSceneDataManager(), frame_count, !interactive);

        if (!interactive) {
            auto &scene_data = rsys->GetSceneDataManager();
            constexpr uint32_t ALL_CASCADES = RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES;
            if (frame_count == RUNTIME_TAKEOVER_FRAME) {
                assert(world->GetShadowCastingLight() == &standby_light_a);
                assert(scene_data.GetNumShadowCastingLights() == 1);
                assert(
                    scene_data.GetLightCascadeCount(0) == ALL_CASCADES
                    && "A light that gains the role is fitted from that frame on, without a graph rebuild."
                );
            } else if (frame_count == RUNTIME_SECOND_FLAG_FRAME) {
                // Creation order decides: the later light never takes the role over.
                assert(world->GetShadowCastingLight() == &standby_light_a);
                assert(scene_data.GetNumShadowCastingLights() == 1);
            } else if (frame_count == RUNTIME_REMOVE_HOLDER_FRAME) {
                assert(
                    world->GetShadowCastingLight() == &standby_light_b
                    && "The remaining flagged light takes the role over."
                );
                assert(scene_data.GetNumShadowCastingLights() == 1);
                assert(scene_data.GetLightCascadeCount(0) == ALL_CASCADES);
            } else if (frame_count == RUNTIME_RESTORE_FRAME) {
                assert(world->GetShadowCastingLight() == &light_comp);
                assert(scene_data.GetNumShadowCastingLights() == 1);
            }
            if (frame_count == GRAPH_REBUILD_FRAME) {
                assert(
                    &scene_data.GetShadowMapTexture(0) == shadow_texture
                    && "Rebuilding the graph must not replace the shadow storage."
                );
            }
        }

        if (interactive) {
            gui_system->PrepareGUI();
            ImGui::SetCurrentContext(gui_system->GetCurrentContext());
            BuildControlWindow(interactive_state, rsys->GetSceneDataManager());
        } else if (frame_count == LIGHT_INVARIANCE_FIRST_FRAME) {
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
            // Swapchain out of date after a retry, skip this frame.
            continue;
        }
        assert(index < rsys->GetPresentProvider().GetImageCount());

        rsys->GetFrameManager().BeginMainCommandBuffer();
        rg->RecordIntoMainCommandBuffer(*rsys);
        auto color = interactive ? interactive_color.get() : rg->GetInternalTextureResource(color_handle);
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
