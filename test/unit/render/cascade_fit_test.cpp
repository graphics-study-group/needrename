#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include <ext/matrix_clip_space.hpp>
#include <ext/matrix_transform.hpp>
#include <glm.hpp>

#include "Render/Shadow/ShadowCascadeFit.h"

using namespace Engine::RenderSystemState;

namespace {
    /// @brief Number of failures reported so far.
    int failures = 0;

    void Check(bool condition, const std::string &what) {
        if (condition) return;
        std::cerr << "FAIL: " << what << '\n';
        failures++;
    }

    bool IsFinite(const glm::mat4 &matrix) {
        for (glm::length_t column = 0; column < 4; column++) {
            for (glm::length_t row = 0; row < 4; row++) {
                if (!std::isfinite(matrix[column][row])) return false;
            }
        }
        return true;
    }

    /// @brief A camera looking at the origin from above, with the aspect ratio of the test scene.
    ShadowCascadeFitParams MakeParams() {
        ShadowCascadeFitParams params{};
        params.direction = glm::normalize(glm::vec3{-0.656f, -0.136f, -0.743f});
        params.camera_view =
            glm::lookAtRH(glm::vec3{0.0f, 2.0f, 6.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});

        // Mirrors `Camera::UpdateProjectionMatrix`, including the Y flip Vulkan needs.
        glm::mat4 projection = glm::perspectiveRH(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 200.0f);
        projection[1][1] *= -1.0f;
        params.camera_projection = projection;

        params.camera_near = 0.1f;
        params.camera_far = 200.0f;
        params.shadow_distance = 100.0f;
        params.cascade_count = 4;
        return params;
    }

    /**
     * @brief World space width of the light space box one cascade covers.
     *
     * The orthographic matrix scales light space by 2 / width along its rows, so
     * the length of the first row gives the scale whatever the light basis is.
     */
    float BoxWidth(const glm::mat4 &light_vp) {
        return 2.0f / glm::length(glm::vec3{light_vp[0][0], light_vp[1][0], light_vp[2][0]});
    }

    /**
     * @brief One of the eight corners of a view frustum slice, in world space.
     */
    glm::vec3 SliceCorner(const ShadowCascadeFitParams &params, float depth, float sign_x, float sign_y) {
        const glm::vec3 view_position{
            sign_x * depth / params.camera_projection[0][0], sign_y * depth / params.camera_projection[1][1], -depth
        };
        const glm::vec3 world{glm::inverse(params.camera_view) * glm::vec4{view_position, 1.0f}};
        return world;
    }

    /// @brief Where a world point lands in one cascade's shadow map.
    struct ShadowMapPosition {
        glm::vec2 uv{0.0f};
        float depth{0.0f};
    };

    ShadowMapPosition Project(const glm::mat4 &light_vp, glm::vec3 world) {
        const glm::vec4 clip = light_vp * glm::vec4{world, 1.0f};
        const glm::vec3 projected = glm::vec3{clip} / clip.w;
        return ShadowMapPosition{.uv = glm::vec2{projected.x, projected.y} * 0.5f + 0.5f, .depth = projected.z};
    }

    bool Inside(const ShadowMapPosition &position, float epsilon = 1e-4f) {
        return position.uv.x >= -epsilon && position.uv.x <= 1.0f + epsilon && position.uv.y >= -epsilon
               && position.uv.y <= 1.0f + epsilon && position.depth >= -epsilon && position.depth <= 1.0f + epsilon;
    }

    /**
     * @brief Split distances follow the practical split scheme and end at the shadow distance.
     */
    void TestSplitsReachTheShadowDistance() {
        const ShadowCascadeFitParams params = MakeParams();
        const ShadowCascadeFit fit = FitShadowCascades(params);

        Check(fit.valid, "a regular camera fits");
        Check(fit.count == params.cascade_count, "the requested cascade count is fitted");
        for (uint32_t cascade = 1; cascade < fit.count; cascade++) {
            Check(fit.splits[cascade] > fit.splits[cascade - 1], "split distances increase");
        }
        Check(
            std::abs(fit.splits[fit.count - 1] - params.shadow_distance) < 1e-3f,
            "the last split is the shadow distance"
        );
        for (uint32_t cascade = fit.count; cascade < SceneDataManager::MAX_SHADOW_CASCADES; ++cascade) {
            Check(
                std::abs(fit.splits[cascade] - params.shadow_distance) < 1e-3f,
                "unused splits repeat the shadow distance"
            );
        }
        for (uint32_t cascade = 0; cascade < fit.count; cascade++) {
            Check(IsFinite(fit.matrices[cascade]), "every cascade matrix is finite");
            Check(std::abs(glm::determinant(fit.matrices[cascade])) > 1e-9f, "every cascade matrix is invertible");
        }
    }

    /**
     * @brief Shadows never reach past the camera's far plane.
     */
    void TestShadowDistanceIsClampedToTheFarPlane() {
        ShadowCascadeFitParams params = MakeParams();
        params.camera_far = 50.0f;
        const ShadowCascadeFit fit = FitShadowCascades(params);

        Check(fit.valid, "a camera closer than the shadow distance still fits");
        Check(std::abs(fit.splits[fit.count - 1] - 50.0f) < 1e-3f, "the last split is the camera far plane");
    }

    /**
     * @brief Every corner of a slice lands inside that slice's cascade.
     *
     * This is the property the whole fit exists for: shadows of the geometry a
     * cascade covers are recorded with it.
     */
    void TestSliceCornersAreCovered() {
        const ShadowCascadeFitParams params = MakeParams();
        const ShadowCascadeFit fit = FitShadowCascades(params);
        const float near_plane = std::max(params.camera_near, params.shadow_distance * 0.001f);

        for (uint32_t cascade = 0; cascade < fit.count; cascade++) {
            const float slice_near = (cascade == 0) ? near_plane : fit.splits[cascade - 1];
            const float slice_far = fit.splits[cascade];
            for (const float depth : {slice_near, slice_far}) {
                for (const float sign_x : {-1.0f, 1.0f}) {
                    for (const float sign_y : {-1.0f, 1.0f}) {
                        const glm::vec3 corner = SliceCorner(params, depth, sign_x, sign_y);
                        const ShadowMapPosition position = Project(fit.matrices[cascade], corner);
                        Check(Inside(position), "a slice corner projects inside its cascade");
                    }
                }
            }
        }
    }

    /**
     * @brief Geometry in front of a slice still casts into it.
     *
     * The fit pushes the near plane one box width towards the light, so a caster
     * hovering above the ground is recorded rather than clipped away.
     */
    void TestCastersInFrontOfTheSliceAreCovered() {
        const ShadowCascadeFitParams params = MakeParams();
        const ShadowCascadeFit fit = FitShadowCascades(params);
        const float near_plane = std::max(params.camera_near, params.shadow_distance * 0.001f);

        for (uint32_t cascade = 0; cascade < fit.count; cascade++) {
            const float slice_near = (cascade == 0) ? near_plane : fit.splits[cascade - 1];

            // The corner the light reaches last, moved towards the light by one
            // metre: a caster between the light and the slice's near face.
            glm::vec3 nearest{0.0f};
            float nearest_depth = std::numeric_limits<float>::max();
            for (const float sign_x : {-1.0f, 1.0f}) {
                for (const float sign_y : {-1.0f, 1.0f}) {
                    const glm::vec3 corner = SliceCorner(params, slice_near, sign_x, sign_y);
                    const glm::vec4 clip = fit.matrices[cascade] * glm::vec4{corner, 1.0f};
                    if (clip.z < nearest_depth) {
                        nearest_depth = clip.z;
                        nearest = corner;
                    }
                }
            }

            const glm::vec3 caster = nearest - params.direction;
            Check(Inside(Project(fit.matrices[cascade], caster)), "a caster in front of the slice is covered");
        }
    }

    /**
     * @brief The fitted box only depends on the frustum's shape, not on where it is.
     *
     * A translated camera keeps the same cascade extents, which is what keeps a
     * moving camera from changing the shadow resolution.
     */
    void TestBoxExtentIsTranslationInvariant() {
        ShadowCascadeFitParams params = MakeParams();
        const ShadowCascadeFit reference = FitShadowCascades(params);

        params.camera_view =
            glm::lookAtRH(glm::vec3{3.0f, 2.0f, 6.0f}, glm::vec3{3.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
        const ShadowCascadeFit translated = FitShadowCascades(params);
        Check(translated.valid, "a translated camera still fits");

        for (uint32_t cascade = 0; cascade < reference.count; cascade++) {
            const float reference_width = BoxWidth(reference.matrices[cascade]);
            const float translated_width = BoxWidth(translated.matrices[cascade]);
            Check(
                std::abs(reference_width - translated_width) < 1e-3f, "translating the camera keeps the cascade width"
            );
        }
    }

    /**
     * @brief Degenerate inputs are rejected instead of producing unusable matrices.
     */
    void TestDegenerateInputsAreRejected() {
        Check(
            !FitShadowCascades([] {
                 ShadowCascadeFitParams params = MakeParams();
                 params.direction = glm::vec3{0.0f};
                 return params;
             }())
                 .valid,
            "a zero light direction is rejected"
        );

        Check(
            !FitShadowCascades([] {
                 ShadowCascadeFitParams params = MakeParams();
                 params.cascade_count = 0;
                 return params;
             }())
                 .valid,
            "zero cascades are rejected"
        );

        Check(
            !FitShadowCascades([] {
                 ShadowCascadeFitParams params = MakeParams();
                 params.cascade_count = SceneDataManager::MAX_SHADOW_CASCADES + 1;
                 return params;
             }())
                 .valid,
            "more cascades than supported are rejected"
        );

        Check(
            !FitShadowCascades([] {
                 ShadowCascadeFitParams params = MakeParams();
                 params.camera_far = params.camera_near;
                 return params;
             }())
                 .valid,
            "an empty depth range is rejected"
        );

        Check(
            !FitShadowCascades([] {
                 ShadowCascadeFitParams params = MakeParams();
                 params.camera_projection = glm::mat4{0.0f};
                 return params;
             }())
                 .valid,
            "a degenerate projection is rejected"
        );

        Check(
            !FitShadowCascades([] {
                 ShadowCascadeFitParams params = MakeParams();
                 params.camera_view = glm::mat4{0.0f};
                 return params;
             }())
                 .valid,
            "a non-invertible view is rejected"
        );
    }

    /**
     * @brief One and three cascades are supported, not just the maximum.
     */
    void TestSupportedCascadeCounts() {
        for (uint32_t count = 1; count <= SceneDataManager::MAX_SHADOW_CASCADES; count++) {
            ShadowCascadeFitParams params = MakeParams();
            params.cascade_count = count;
            const ShadowCascadeFit fit = FitShadowCascades(params);
            Check(fit.valid && fit.count == count, "every supported cascade count fits");
            Check(
                std::abs(fit.splits[count - 1] - params.shadow_distance) < 1e-3f, "the last used split ends the range"
            );
        }
    }
} // namespace

int main() {
    TestSplitsReachTheShadowDistance();
    TestShadowDistanceIsClampedToTheFarPlane();
    TestSliceCornersAreCovered();
    TestCastersInFrontOfTheSliceAreCovered();
    TestBoxExtentIsTranslationInvariant();
    TestDegenerateInputsAreRejected();
    TestSupportedCascadeCounts();

    if (failures != 0) {
        std::cerr << failures << " cascade fit check(s) failed.\n";
        return 1;
    }
    std::cout << "All shadow cascade fit checks passed.\n";
    return 0;
}
