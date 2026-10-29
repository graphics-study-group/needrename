#ifndef RENDER_SHADOW_SHADOWCASCADEFIT_INCLUDED
#define RENDER_SHADOW_SHADOWCASCADEFIT_INCLUDED

#include "Render/RenderSystem/SceneDataManager.h"
#include "Render/render_export.h"

#include <cstdint>
#include <glm.hpp>

namespace Engine::RenderSystemState {
    /**
     * @brief Inputs of one directional light's cascade fit.
     *
     * The camera is the frustum the cascades cover; the light contributes only
     * its direction, so moving the light never changes the fitted cascades.
     */
    struct RENDER_API ShadowCascadeFitParams {
        /// Unit vector the light travels along, pointing from the light towards the scene.
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        /// View matrix of the camera the cascades are fitted to.
        glm::mat4 camera_view{1.0f};
        /// Projection matrix of that camera, whose P00 and P11 give the frustum's field of view.
        glm::mat4 camera_projection{1.0f};
        /// Near clipping distance of that camera.
        float camera_near{0.1f};
        /// Far clipping distance of that camera.
        float camera_far{100.0f};
        /// Positive distance from the camera that shadows reach.
        float shadow_distance{100.0f};
        /// Number of cascades to fit, within `1..SceneDataManager::MAX_SHADOW_CASCADES`.
        uint32_t cascade_count{SceneDataManager::MAX_SHADOW_CASCADES};
    };

    /**
     * @brief One directional light's cascades, fitted to slices of a camera frustum.
     */
    struct RENDER_API ShadowCascadeFit {
        /// One projection * view matrix per cascade; entries beyond `count` stay zero.
        glm::mat4 matrices[SceneDataManager::MAX_SHADOW_CASCADES]{};
        /// Cascade end distances along the camera's view axis, where the component
        /// at `c` ends cascade `c`; components beyond `count` repeat the last one.
        glm::vec4 splits{0.0f};
        /// Number of valid cascades.
        uint32_t count{0};
        /// Whether the fit produced usable data.
        bool valid{false};
    };

    /**
     * @brief Fit a directional light's cascades to slices of a camera frustum.
     *
     * The fit never reads the light's world position, so translating the light
     * leaves the cascades unchanged. Slices are derived with the practical split
     * scheme, and every cascade gets a tight orthographic projection of the light
     * space bounding box of its slice's eight corners, with the near plane pushed
     * towards the light by the box width, a lateral margin and texel snapped
     * extents in light space X/Y. A non-finite intermediate value invalidates the
     * whole fit.
     *
     * @param params Direction, camera frustum, shadow distance and cascade count.
     * @return The fitted cascades; `valid` is false when no fit could be made.
     */
    RENDER_API ShadowCascadeFit FitShadowCascades(const ShadowCascadeFitParams &params) noexcept;
} // namespace Engine::RenderSystemState

#endif // RENDER_SHADOW_SHADOWCASCADEFIT_INCLUDED
