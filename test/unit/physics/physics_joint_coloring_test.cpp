// Tests for the joint color bucket builder:
//   - an empty graph has no colors and an empty bucket list
//   - a quadruped's hinge graph colors into four colors (the trunk's four hip
//     joints pairwise conflict), and its fixed graph into one
//   - a disabled joint consumes no color
//   - a joint attached to a kinematic body is not a conflict
//   - many independent copies of the same graph keep the single-robot color
//     count, and every color's slice is contiguous, ascending by slot, and
//     wide enough to fill workgroups
//
// The last case is the one that would fail if the bucket list were given a
// secondary order by connected component: that would destroy the ascending
// slot order inside a color, packing every warp with one robot's joints.

#include "Physics/PhysicsJointColoring.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace Engine;

static bool g_pass = true;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                                        \
            g_pass = false;                                                                                            \
        }                                                                                                              \
    } while (0)

namespace {
    constexpr uint32_t kTrunk = 0u;
    constexpr uint32_t kBodiesPerLeg = 3u;
    constexpr uint32_t kHingeJointsPerLeg = 3u;

    // Four legs, three bodies each (hip, thigh, calf): the quadruped's hinge
    // graph. Every leg is a three-joint chain hanging off the shared trunk.
    std::vector<JointColorRef> MakeQuadrupedHingeJoints(uint32_t leg_count = 4u) {
        std::vector<JointColorRef> joints;
        for (uint32_t leg = 0u; leg < leg_count; ++leg) {
            const uint32_t hip = 1u + leg * kBodiesPerLeg;
            const uint32_t thigh = hip + 1u;
            const uint32_t calf = hip + 2u;
            joints.push_back({kTrunk, hip, 1u});
            joints.push_back({hip, thigh, 1u});
            joints.push_back({thigh, calf, 1u});
        }
        return joints;
    }

    // IMU link on the trunk plus one foot per leg: all disjoint.
    std::vector<JointColorRef> MakeQuadrupedFixedJoints(uint32_t leg_count = 4u) {
        std::vector<JointColorRef> joints;
        joints.push_back({kTrunk, 1u + leg_count * kBodiesPerLeg, 1u}); // imu
        for (uint32_t leg = 0u; leg < leg_count; ++leg) {
            const uint32_t calf = 1u + leg * kBodiesPerLeg + 2u;
            const uint32_t foot = 1u + leg_count * kBodiesPerLeg + 1u + leg;
            joints.push_back({calf, foot, 1u});
        }
        return joints;
    }

    uint32_t QuadrupedBodyCount(uint32_t leg_count = 4u) {
        return 1u + leg_count * kBodiesPerLeg + 1u + leg_count; // trunk + legs + imu + feet
    }

    /// Every color's slice must be contiguous, ascending by slot, and its
    /// length must equal the range table's width.
    bool SlicesAreWellFormed(const JointColorBuckets &buckets, uint32_t alive_joint_count) {
        if (buckets.offsets.size() != static_cast<size_t>(buckets.color_count) + 1u) return false;
        if (buckets.offsets.front() != 0u) return false;
        if (buckets.offsets.back() != alive_joint_count) return false;
        if (buckets.ids.size() != alive_joint_count) return false;

        for (uint32_t color = 0u; color < buckets.color_count; ++color) {
            const uint32_t begin = buckets.offsets[color];
            const uint32_t end = buckets.offsets[color + 1u];
            for (uint32_t i = begin + 1u; i < end; ++i) {
                if (buckets.ids[i] <= buckets.ids[i - 1u]) return false;
            }
        }
        return true;
    }
} // namespace

int main() {
    // ── Empty graph ──────────────────────────────────────────────────────────

    {
        const JointColorBuckets buckets = BuildJointColorBuckets({}, {}, 0u);
        CHECK(buckets.color_count == 0u);
        CHECK(buckets.ids.empty());
        CHECK(buckets.offsets.size() == 1u && buckets.offsets[0] == 0u);
    }

    // ── Quadruped: four hinge colors, one fixed color ────────────────────────

    const std::vector<JointColorRef> hinge_joints = MakeQuadrupedHingeJoints();
    const std::vector<JointColorRef> fixed_joints = MakeQuadrupedFixedJoints();
    const std::vector<uint32_t> all_dynamic(QuadrupedBodyCount(), 0u);

    {
        const JointColorBuckets buckets =
            BuildJointColorBuckets(hinge_joints, all_dynamic, QuadrupedBodyCount());
        CHECK(buckets.color_count == 4u);
        CHECK(SlicesAreWellFormed(buckets, static_cast<uint32_t>(hinge_joints.size())));
        CHECK(buckets.offsets[1] - buckets.offsets[0] == 5u);
        CHECK(buckets.offsets[2] - buckets.offsets[1] == 5u);
        CHECK(buckets.offsets[3] - buckets.offsets[2] == 1u);
        CHECK(buckets.offsets[4] - buckets.offsets[3] == 1u);
    }

    {
        const JointColorBuckets buckets =
            BuildJointColorBuckets(fixed_joints, all_dynamic, QuadrupedBodyCount());
        CHECK(buckets.color_count == 1u);
        CHECK(buckets.ids.size() == fixed_joints.size());
        CHECK(SlicesAreWellFormed(buckets, static_cast<uint32_t>(fixed_joints.size())));
    }

    // ── A disabled joint consumes no color ───────────────────────────────────

    {
        std::vector<JointColorRef> disabled = hinge_joints;
        disabled[6].alive = 0u; // RR_hip: the last leg attachment that raised a color
        const JointColorBuckets buckets =
            BuildJointColorBuckets(disabled, all_dynamic, QuadrupedBodyCount());
        CHECK(buckets.color_count == 3u);
        CHECK(buckets.ids.size() == disabled.size() - 1u);
        CHECK(SlicesAreWellFormed(buckets, static_cast<uint32_t>(buckets.ids.size())));
        for (uint32_t slot : buckets.ids) {
            CHECK(slot != 6u);
        }
    }

    // ── A kinematic body is never a conflict ─────────────────────────────────

    {
        std::vector<uint32_t> trunk_kinematic(QuadrupedBodyCount(), 0u);
        trunk_kinematic[kTrunk] = 1u;
        const JointColorBuckets buckets =
            BuildJointColorBuckets(hinge_joints, trunk_kinematic, QuadrupedBodyCount());
        CHECK(buckets.color_count == 2u);
        CHECK(buckets.offsets[1] - buckets.offsets[0] == 8u);
        CHECK(buckets.offsets[2] - buckets.offsets[1] == 4u);
        CHECK(SlicesAreWellFormed(buckets, static_cast<uint32_t>(hinge_joints.size())));
    }

    // ── Many independent copies keep the single-robot color count ────────────

    {
        constexpr uint32_t kCopies = 1024u;
        const uint32_t bodies_per_copy = QuadrupedBodyCount();
        const uint32_t joints_per_copy = static_cast<uint32_t>(hinge_joints.size());
        constexpr uint32_t kWorkgroupSize = 64u;

        // Interleave the copies by joint index so that a secondary order by
        // connected component cannot coincide with the slot order.
        std::vector<JointColorRef> many;
        many.reserve(static_cast<size_t>(kCopies) * joints_per_copy);
        for (uint32_t j = 0u; j < joints_per_copy; ++j) {
            for (uint32_t copy = 0u; copy < kCopies; ++copy) {
                const JointColorRef &source = hinge_joints[j];
                JointColorRef ref = source;
                ref.obj1_index += copy * bodies_per_copy;
                ref.obj2_index += copy * bodies_per_copy;
                many.push_back(ref);
            }
        }

        std::vector<uint32_t> dynamic(kCopies * bodies_per_copy, 0u);
        const JointColorBuckets buckets =
            BuildJointColorBuckets(many, dynamic, static_cast<uint32_t>(dynamic.size()));
        CHECK(buckets.color_count == 4u);
        CHECK(buckets.ids.size() == many.size());
        CHECK(SlicesAreWellFormed(buckets, static_cast<uint32_t>(many.size())));

        // Every color must be wide enough to fill workgroups; the greedy colors
        // split a copy's twelve joints as 5 / 5 / 1 / 1, so the narrowest is one
        // joint per copy.
        for (uint32_t color = 0u; color < buckets.color_count; ++color) {
            const uint32_t width = buckets.offsets[color + 1u] - buckets.offsets[color];
            CHECK(width >= kWorkgroupSize);
        }
    }

    if (!g_pass) {
        std::cerr << "joint coloring tests failed" << std::endl;
        return 1;
    }
    std::cout << "joint coloring tests passed" << std::endl;
    return 0;
}
