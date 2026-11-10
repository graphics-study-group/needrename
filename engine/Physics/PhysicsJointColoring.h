#ifndef ENGINE_PHYSICS_PHYSICSJOINTCOLORING_INCLUDED
#define ENGINE_PHYSICS_PHYSICSJOINTCOLORING_INCLUDED

#include "physics_export.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Engine {

    /**
     * @brief One joint's contribution to the joint color graph.
     *
     * A joint with `alive == 0` is not part of the graph and consumes no color.
     * A body index at or beyond the body count, or a body marked kinematic, can
     * never conflict: the solver does not write it.
     */
    struct PHYSICS_API JointColorRef {
        uint32_t obj1_index{0xFFFFFFFFu};
        uint32_t obj2_index{0xFFFFFFFFu};
        uint32_t alive{0};
    };

    /**
     * @brief A joint type's color grouping, ready to upload.
     *
     * `ids` holds every alive joint's slot id, grouped by color and ascending
     * inside a color. `offsets[c]` is the first index of color `c`'s slice and
     * `offsets[color_count]` is one past the last, so a color's slice is
     * `[offsets[c], offsets[c + 1])` and its length is that color's joint count.
     */
    struct PHYSICS_API JointColorBuckets {
        std::vector<uint32_t> ids{};
        std::vector<uint32_t> offsets{}; ///< size = color_count + 1
        uint32_t color_count{0};
    };

    /**
     * @brief Color the joint graph and build its bucket list.
     *
     * Greedy first-fit over the joints' slot order: a joint takes the lowest
     * color no joint on either of its writable bodies already uses. Deterministic
     * for a given input. Kinematic bodies are not writable, so several joints
     * attached to the same kinematic body may share a color.
     *
     * @param joints Joint graph edges, indexed by slot.
     * @param body_is_kinematic Per-body kinematic flags; may be shorter than the
     *        body count, in which case missing entries are treated as dynamic.
     * @param body_count Number of rigid body slots.
     * @return The bucket list and color count. A graph with no alive joint has
     *         `color_count == 0` and `offsets == {0}`.
     */
    PHYSICS_API JointColorBuckets BuildJointColorBuckets(
        std::span<const JointColorRef> joints, std::span<const uint32_t> body_is_kinematic, uint32_t body_count
    );

} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSJOINTCOLORING_INCLUDED
