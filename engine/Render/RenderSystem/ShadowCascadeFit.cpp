#include "ShadowCascadeFit.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ext/matrix_clip_space.hpp>
#include <ext/matrix_transform.hpp>
#include <limits>

namespace Engine::RenderSystemState {
    namespace {
        /// Practical split scheme weight: 0 is a purely uniform split, 1 is purely logarithmic.
        constexpr float CASCADE_SPLIT_LAMBDA = 0.8f;

        /// Fraction of the shadow distance the first cascade's near plane is clamped to.
        constexpr float CASCADE_NEAR_CLAMP_FRACTION = 0.001f;

        /// Lateral margin applied to a cascade's orthographic extents.
        constexpr float CASCADE_LATERAL_MARGIN = 1.05f;

        /// Distance kept between the light eye and the near plane after the cascade box
        /// width has been subtracted from it.
        constexpr float CASCADE_NEAR_EPSILON = 0.01f;

        /**
         * @brief Whether every component of a matrix is finite.
         */
        bool IsFinite(const glm::mat4 &matrix) {
            for (glm::length_t column = 0; column < 4; column++) {
                for (glm::length_t row = 0; row < 4; row++) {
                    if (!std::isfinite(matrix[column][row])) return false;
                }
            }
            return true;
        }

        /**
         * @brief Whether every component of a vector is finite.
         */
        bool IsFinite(glm::vec3 vector) {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        }

    } // namespace

    ShadowCascadeFit FitShadowCascades(const ShadowCascadeFitParams &params) noexcept {
        ShadowCascadeFit result{};
        if (params.cascade_count == 0 || params.cascade_count > SceneDataManager::MAX_SHADOW_CASCADES) return result;

        // The basis below normalizes the direction in place, so the fit works on a copy.
        glm::vec3 direction = params.direction;

        // The shadow distance bounds both ends of the fitted range: shadows are
        // neither needed closer than the camera's near plane nor farther than
        // either the camera's far plane or the light's shadow distance.
        const float far_plane = std::min(params.camera_far, params.shadow_distance);
        const float near_plane = std::max(params.camera_near, params.shadow_distance * CASCADE_NEAR_CLAMP_FRACTION);
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || !(far_plane > near_plane)) return result;

        const float proj_x = params.camera_projection[0][0];
        const float proj_y = params.camera_projection[1][1];
        if (!std::isfinite(proj_x) || !std::isfinite(proj_y) || std::abs(proj_x) < 1e-9f || std::abs(proj_y) < 1e-9f) {
            return result;
        }
        const glm::mat4 inverse_view = glm::inverse(params.camera_view);
        if (!IsFinite(inverse_view)) return result;

        // Practical split scheme: split_i = mix(near * (far / near)^(i / N),
        // near + (far - near) * i / N, lambda).
        float slice_boundaries[SceneDataManager::MAX_SHADOW_CASCADES + 1]{};
        slice_boundaries[0] = near_plane;
        for (uint32_t i = 1; i <= params.cascade_count; i++) {
            const float fraction = static_cast<float>(i) / static_cast<float>(params.cascade_count);
            const float logarithmic = near_plane * std::pow(far_plane / near_plane, fraction);
            const float uniform = near_plane + (far_plane - near_plane) * fraction;
            slice_boundaries[i] = logarithmic * CASCADE_SPLIT_LAMBDA + uniform * (1.0f - CASCADE_SPLIT_LAMBDA);
        }
        slice_boundaries[params.cascade_count] = far_plane;

        SDL_LogDebug(
            SDL_LOG_CATEGORY_RENDER,
            "Cascade splits for shadow distance %.3f (near %.4f, far %.3f): %.4f / %.4f / %.4f / %.4f / %.4f",
            params.shadow_distance,
            near_plane,
            far_plane,
            slice_boundaries[0],
            slice_boundaries[std::min(1u, params.cascade_count)],
            slice_boundaries[std::min(2u, params.cascade_count)],
            slice_boundaries[std::min(3u, params.cascade_count)],
            slice_boundaries[params.cascade_count]
        );

        // Stable light basis: world Y is the reference up axis, with a switch to
        // world Z for a light pointing (nearly) straight down or up, where Y
        // degenerates.
        const float direction_length = glm::length(direction);
        if (!std::isfinite(direction_length) || direction_length < 1e-6f) return result;
        direction /= direction_length;
        const glm::vec3 reference_up = std::abs(glm::dot(direction, glm::vec3{0.0f, 1.0f, 0.0f})) > 0.99f
                                           ? glm::vec3{0.0f, 0.0f, 1.0f}
                                           : glm::vec3{0.0f, 1.0f, 0.0f};
        // The shadow camera sits behind the light and looks along its
        // direction of travel, so that the nearest surface in the map is the
        // one the light reaches first.
        const glm::vec3 forward = direction;
        const glm::vec3 right = glm::cross(forward, reference_up);
        const float right_length = glm::length(right);
        if (!std::isfinite(right_length) || right_length < 1e-6f) return result;
        const glm::vec3 right_axis = right / right_length;
        const glm::vec3 up_axis = glm::cross(right_axis, forward);

        for (uint32_t cascade = 0; cascade < params.cascade_count; cascade++) {
            const float slice_near = slice_boundaries[cascade];
            const float slice_far = slice_boundaries[cascade + 1];

            // Light space coordinates of the eight corners of the slice. The
            // projection matrix gives the tangent of half the field of view, so
            // the corners at a view depth d are (+-d / P00, +-d / P11, -d).
            glm::vec3 minimum{std::numeric_limits<float>::max()};
            glm::vec3 maximum{-std::numeric_limits<float>::max()};
            for (const float depth : {slice_near, slice_far}) {
                for (const float sign_x : {-1.0f, 1.0f}) {
                    for (const float sign_y : {-1.0f, 1.0f}) {
                        const glm::vec3 view_position{sign_x * depth / proj_x, sign_y * depth / proj_y, -depth};
                        const glm::vec3 world_position{inverse_view * glm::vec4{view_position, 1.0f}};
                        const glm::vec3 light_position{
                            glm::dot(world_position, right_axis),
                            glm::dot(world_position, up_axis),
                            glm::dot(world_position, forward)
                        };
                        minimum = glm::min(minimum, light_position);
                        maximum = glm::max(maximum, light_position);
                    }
                }
            }
            if (!IsFinite(minimum) || !IsFinite(maximum)) return result;

            // "As deep as it is wide": the near plane reaches one box width
            // towards the light, so casters in front of the slice still cast.
            const float box_width = std::max(maximum.x - minimum.x, maximum.y - minimum.y);
            const float diameter = box_width * CASCADE_LATERAL_MARGIN;
            if (!std::isfinite(diameter) || !(diameter > 0.0f)) return result;

            // Snap the orthographic extents to whole shadow texels in light
            // space X/Y only. The light space grid is anchored in the world, so
            // a moving camera cannot make shadow edges crawl.
            const float texel_size = diameter / static_cast<float>(SceneDataManager::SHADOWMAP_SIZE);
            if (!std::isfinite(texel_size) || !(texel_size > 0.0f)) return result;
            const float center_x = std::floor((minimum.x + maximum.x) * 0.5f / texel_size) * texel_size;
            const float center_y = std::floor((minimum.y + maximum.y) * 0.5f / texel_size) * texel_size;
            const float half_diameter = diameter * 0.5f;
            if (!std::isfinite(center_x) || !std::isfinite(center_y)) return result;

            // Place the light eye one box width (plus an epsilon) in front of the
            // slice's near face, then push the near plane towards the light by
            // exactly that box width.
            const float eye_depth = minimum.z - box_width - CASCADE_NEAR_EPSILON;
            const float ortho_near = CASCADE_NEAR_EPSILON;
            const float ortho_far = maximum.z - eye_depth;
            if (!std::isfinite(eye_depth) || !(ortho_far > ortho_near)) return result;

            const glm::vec3 eye = forward * eye_depth;
            const glm::mat4 view = glm::lookAtRH(eye, eye + forward, reference_up);
            // Vulkan's clip space points y down, so the box is laid out with its
            // top edge as `bottom`. Swapping the two (instead of negating P11)
            // mirrors the extent about the box centre; negating only P11 would
            // mirror it about light space y = 0 and push every cascade that is
            // not centred on the light axis partly out of the clip range.
            glm::mat4 projection = glm::ortho(
                center_x - half_diameter,
                center_x + half_diameter,
                center_y + half_diameter,
                center_y - half_diameter,
                ortho_near,
                ortho_far
            );
            result.matrices[cascade] = projection * view;
            if (!IsFinite(result.matrices[cascade])) return result;

            result.splits[cascade] = slice_boundaries[cascade + 1];
        }
        for (uint32_t cascade = params.cascade_count; cascade < SceneDataManager::MAX_SHADOW_CASCADES; cascade++) {
            result.splits[cascade] = far_plane;
        }
        result.count = params.cascade_count;
        result.valid = true;
        return result;
    }
} // namespace Engine::RenderSystemState
