#include "PhysicsJointColoring.h"

#include <algorithm>

namespace Engine {

    JointColorBuckets BuildJointColorBuckets(
        std::span<const JointColorRef> joints, std::span<const uint32_t> body_is_kinematic, uint32_t body_count
    ) {
        JointColorBuckets buckets;
        const uint32_t joint_count = static_cast<uint32_t>(joints.size());

        // A body is a conflict source only while the solver can write it: an
        // out-of-range slot has no state, and a kinematic body is never written.
        auto is_writable_body = [&](uint32_t body) {
            return body < body_count && (body >= body_is_kinematic.size() || body_is_kinematic[body] == 0u);
        };

        std::vector<std::vector<uint32_t>> body_colors(body_count);
        std::vector<uint32_t> colors(joint_count, 0u);

        for (uint32_t slot = 0; slot < joint_count; ++slot) {
            const JointColorRef &joint = joints[slot];
            if (joint.alive == 0u) continue;

            uint32_t color = 0u;
            auto conflicts = [&]() {
                for (uint32_t body : {joint.obj1_index, joint.obj2_index}) {
                    if (!is_writable_body(body)) continue;
                    const std::vector<uint32_t> &used = body_colors[body];
                    if (std::find(used.begin(), used.end(), color) != used.end()) return true;
                }
                return false;
            };
            while (conflicts()) ++color;

            colors[slot] = color;
            for (uint32_t body : {joint.obj1_index, joint.obj2_index}) {
                if (is_writable_body(body)) body_colors[body].push_back(color);
            }
            buckets.color_count = std::max(buckets.color_count, color + 1u);
        }

        // Count each color's joints, then turn the counts into slice starts.
        buckets.offsets.assign(buckets.color_count + 1u, 0u);
        for (uint32_t slot = 0; slot < joint_count; ++slot) {
            if (joints[slot].alive != 0u) ++buckets.offsets[colors[slot] + 1u];
        }
        for (uint32_t color = 0; color < buckets.color_count; ++color) {
            buckets.offsets[color + 1u] += buckets.offsets[color];
        }

        // Place each alive joint in its color's slice, ascending by slot inside
        // the slice: the slot order already carries whatever locality the scene
        // has, and a secondary order by connected component would pack a warp
        // with one robot's joints instead of many robots'.
        std::vector<uint32_t> cursor(buckets.offsets.begin(), buckets.offsets.end() - 1);
        buckets.ids.resize(buckets.offsets.back());
        for (uint32_t slot = 0; slot < joint_count; ++slot) {
            if (joints[slot].alive == 0u) continue;
            buckets.ids[cursor[colors[slot]]++] = slot;
        }

        return buckets;
    }

} // namespace Engine
