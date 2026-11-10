// xpbd_joint_solve.glsl — Shared per-joint constraint evaluation and direct write.
//
// Each entry point reads one joint by slot id, evaluates both of its constraints
// against the same pose, and adds the summed correction to both bodies. A joint
// invocation is the only writer of its two bodies in a color pass (a color's
// joints are body-disjoint), so the write needs no accumulation buffer, no
// contribution count, no segmented reduction and no per-body apply pass. The
// correction is applied at full strength: nothing divides it by the number of
// constraints touching a body, so the two ends of a joint receive the same
// equal-and-opposite impulse and momentum is conserved.
//
// The including shader MUST:
//   - declare the descriptor / lagrange / alive bindings of the joint type it
//     solves, plus the shared rigid-body bindings (position and rotation are
//     read-write, the rest read-only),
//   - define XPBD_JOINT_SOLVE_HINGE and/or XPBD_JOINT_SOLVE_FIXED before this
//     include, so a shader that solves one type never references the other
//     type's bindings.
//
// Binding names this file expects:
//   hinge: hinge_joints, hinge_axis_lagrange, hinge_anchor_lagrange, hinge_joint_alive
//   fixed: fixed_joints, fixed_rotation_lagrange, fixed_position_lagrange, fixed_joint_alive
//   both : rigid_body_alive, rigid_body_center_position, rigid_body_center_rotation,
//          rigid_body_mass, rigid_body_inverse_inertia, rigid_body_is_kinematic

#ifndef XPBD_JOINT_SOLVE_GLSL
#define XPBD_JOINT_SOLVE_GLSL

#include "xpbd_math.glsl"
#include "xpbd_joint_math.glsl"

const float kJointSolveEpsilon = 1e-8;

/// @brief Add one joint's summed correction to one body.
///
/// The linear part is added to the COM position and the angular part is
/// integrated into the orientation with the same quaternion increment the
/// per-body position apply used, so a joint correction and a contact correction
/// still compose the same way.
void joint_add_body_correction(uint body, vec3 linear, vec3 angular) {
    if (rigid_body_is_kinematic.v[body] != 0u) return;

    rigid_body_center_position.v[body].xyz += linear;

    if (length(angular) > kJointSolveEpsilon) {
        vec4 orientation = rigid_body_center_rotation.v[body];
        orientation += 0.5 * quat_mul(vec4(angular, 0.0), orientation);
        rigid_body_center_rotation.v[body] = quat_normalize(orientation);
    }
}

#ifdef XPBD_JOINT_SOLVE_HINGE
/// @brief Solve one hinge joint: align the axes, then coincide the anchors.
///
/// Both constraints are linearized at the joint's input pose and their deltas
/// are summed, matching the previous two-scatter-then-one-apply formulation.
void joint_solve_hinge(uint jidx, float dt) {
    if (hinge_joint_alive.v[jidx] == 0u) return;

    HingeJointComDescriptor joint = hinge_joints.v[jidx];
    uint body_count = rigid_body_alive.v.length();
    uint obj1_idx = joint.obj1_index;
    uint obj2_idx = joint.obj2_index;
    if (obj1_idx >= body_count || obj2_idx >= body_count) return;
    if (rigid_body_alive.v[obj1_idx] == 0u || rigid_body_alive.v[obj2_idx] == 0u) return;

    bool scripted_obj1 = (rigid_body_is_kinematic.v[obj1_idx] != 0u);
    bool scripted_obj2 = (rigid_body_is_kinematic.v[obj2_idx] != 0u);
    if (scripted_obj1 && scripted_obj2) return;

    // A kinematic body is immovable: zero inverse mass and no inverse-inertia
    // term, whatever mass and inertia it was authored with.
    float inv_mass_obj1 =
        (!scripted_obj1 && rigid_body_mass.v[obj1_idx] > 1e-12) ? (1.0 / rigid_body_mass.v[obj1_idx]) : 0.0;
    float inv_mass_obj2 =
        (!scripted_obj2 && rigid_body_mass.v[obj2_idx] > 1e-12) ? (1.0 / rigid_body_mass.v[obj2_idx]) : 0.0;

    vec3 pos1 = rigid_body_center_position.v[obj1_idx].xyz;
    vec3 pos2 = rigid_body_center_position.v[obj2_idx].xyz;
    vec4 ori1 = rigid_body_center_rotation.v[obj1_idx];
    vec4 ori2 = rigid_body_center_rotation.v[obj2_idx];

    mat4 inv_inertia1 = rigid_body_inverse_inertia.v[obj1_idx];
    mat4 inv_inertia2 = rigid_body_inverse_inertia.v[obj2_idx];

    float tilde_alpha = joint.compliance / (dt * dt);

    vec3 lin1 = vec3(0.0);
    vec3 ang1 = vec3(0.0);
    vec3 lin2 = vec3(0.0);
    vec3 ang2 = vec3(0.0);

    // ================================================================
    // Derive obj2-local values from obj1-local + initial relative transform.
    //   obj2_local_axis   = rel_rot⁻¹ * hinge_axis_obj1
    //   obj2_local_anchor = rel_rot⁻¹ * (hinge_anchor_obj1 - rel_pos_local)
    // ================================================================
    vec4 q_rel_inv = quat_inverse(joint.initial_rel_rotation);
    vec3 obj2_local_axis = quat_rotate(q_rel_inv, joint.hinge_axis_obj1.xyz);
    vec3 obj2_local_anchor = quat_rotate(q_rel_inv, joint.hinge_anchor_obj1.xyz - joint.initial_rel_pos_local.xyz);

    // =====================================================================
    // Constraint 1: Aligned axis constraint
    // C_align = |cross(axis1_world, axis2_world)|
    // =====================================================================
    vec3 axis1_world = normalize(quat_rotate(ori1, joint.hinge_axis_obj1.xyz));
    vec3 axis2_world = normalize(quat_rotate(ori2, obj2_local_axis));
    vec3 angular_correction = cross(axis1_world, axis2_world);
    float angular_norm = length(angular_correction);

    if (angular_norm > kJointSolveEpsilon) {
        vec3 n = angular_correction / angular_norm;
        vec3 obj1_world_inv_i_n = apply_world_inv_inertia(inv_inertia1, ori1, n);
        vec3 obj2_world_inv_i_n = apply_world_inv_inertia(inv_inertia2, ori2, n);
        float w1 = scripted_obj1 ? 0.0 : dot(obj1_world_inv_i_n, n);
        float w2 = scripted_obj2 ? 0.0 : dot(obj2_world_inv_i_n, n);
        float denom = w1 + w2 + tilde_alpha;

        if (denom > kJointSolveEpsilon) {
            float dlambda = (angular_norm - hinge_axis_lagrange.v[jidx] * tilde_alpha) / denom;

            if (!scripted_obj1) {
                ang1 += obj1_world_inv_i_n * dlambda;
            }
            if (!scripted_obj2) {
                ang2 += -(obj2_world_inv_i_n * dlambda);
            }

            hinge_axis_lagrange.v[jidx] += dlambda;
        }
    }

    // =====================================================================
    // Constraint 2: Attachment point constraint
    // C_pos = |attach_point2_world - attach_point1_world|
    // =====================================================================
    vec3 r1 = quat_rotate(ori1, joint.hinge_anchor_obj1.xyz);
    vec3 r2 = quat_rotate(ori2, obj2_local_anchor);
    vec3 attach1_world = pos1 + r1;
    vec3 attach2_world = pos2 + r2;
    vec3 positional_correction = attach2_world - attach1_world;
    float positional_norm = length(positional_correction);

    if (positional_norm > kJointSolveEpsilon) {
        vec3 n = positional_correction / positional_norm;
        vec3 r1_n = cross(r1, n);
        vec3 r2_n = cross(r2, n);
        vec3 i1_r1n = apply_world_inv_inertia(inv_inertia1, ori1, r1_n);
        vec3 i2_r2n = apply_world_inv_inertia(inv_inertia2, ori2, r2_n);
        float w1 = inv_mass_obj1 + (scripted_obj1 ? 0.0 : dot(i1_r1n, r1_n));
        float w2 = inv_mass_obj2 + (scripted_obj2 ? 0.0 : dot(i2_r2n, r2_n));
        float denom = w1 + w2 + tilde_alpha;

        if (denom > kJointSolveEpsilon) {
            float dlambda = (positional_norm - hinge_anchor_lagrange.v[jidx] * tilde_alpha) / denom;
            vec3 impulse = dlambda * n;

            if (!scripted_obj1) {
                lin1 += inv_mass_obj1 * impulse;
                ang1 += apply_world_inv_inertia(inv_inertia1, ori1, cross(r1, impulse));
            }

            if (!scripted_obj2) {
                lin2 += -(inv_mass_obj2 * impulse);
                ang2 += -apply_world_inv_inertia(inv_inertia2, ori2, cross(r2, impulse));
            }

            hinge_anchor_lagrange.v[jidx] += dlambda;
        }
    }

    joint_add_body_correction(obj1_idx, lin1, ang1);
    joint_add_body_correction(obj2_idx, lin2, ang2);
}
#endif // XPBD_JOINT_SOLVE_HINGE

#ifdef XPBD_JOINT_SOLVE_FIXED
/// @brief Solve one fixed joint: drive the relative rotation back, then coincide
/// the symmetric attachment points.
void joint_solve_fixed(uint jidx, float dt) {
    if (fixed_joint_alive.v[jidx] == 0u) return;

    FixedJointComDescriptor joint = fixed_joints.v[jidx];
    uint body_count = rigid_body_alive.v.length();
    uint obj1_idx = joint.obj1_index;
    uint obj2_idx = joint.obj2_index;
    if (obj1_idx >= body_count || obj2_idx >= body_count) return;
    if (rigid_body_alive.v[obj1_idx] == 0u || rigid_body_alive.v[obj2_idx] == 0u) return;

    bool scripted_obj1 = (rigid_body_is_kinematic.v[obj1_idx] != 0u);
    bool scripted_obj2 = (rigid_body_is_kinematic.v[obj2_idx] != 0u);
    if (scripted_obj1 && scripted_obj2) return;

    float inv_mass_obj1 =
        (!scripted_obj1 && rigid_body_mass.v[obj1_idx] > 1e-12) ? (1.0 / rigid_body_mass.v[obj1_idx]) : 0.0;
    float inv_mass_obj2 =
        (!scripted_obj2 && rigid_body_mass.v[obj2_idx] > 1e-12) ? (1.0 / rigid_body_mass.v[obj2_idx]) : 0.0;

    vec3 pos1 = rigid_body_center_position.v[obj1_idx].xyz;
    vec3 pos2 = rigid_body_center_position.v[obj2_idx].xyz;
    vec4 ori1 = rigid_body_center_rotation.v[obj1_idx];
    vec4 ori2 = rigid_body_center_rotation.v[obj2_idx];

    mat4 inv_inertia1 = rigid_body_inverse_inertia.v[obj1_idx];
    mat4 inv_inertia2 = rigid_body_inverse_inertia.v[obj2_idx];

    float tilde_alpha = joint.compliance / (dt * dt);

    vec3 lin1 = vec3(0.0);
    vec3 ang1 = vec3(0.0);
    vec3 lin2 = vec3(0.0);
    vec3 ang2 = vec3(0.0);

    // =====================================================================
    // Constraint 1: Rotation constraint
    // Drive relative rotation toward initial snapshot.
    // =====================================================================
    vec4 q1_inv = quat_inverse(ori1);
    vec4 q_initial_inv = quat_inverse(joint.initial_rel_rotation);
    vec4 q_error = quat_mul(quat_mul(ori2, q_initial_inv), q1_inv);
    vec3 angular_correction = quat_to_rotation_vec(q_error);
    float angular_norm = length(angular_correction);

    if (angular_norm > kJointSolveEpsilon) {
        vec3 n = angular_correction / angular_norm;
        vec3 obj1_world_inv_i_n = apply_world_inv_inertia(inv_inertia1, ori1, n);
        vec3 obj2_world_inv_i_n = apply_world_inv_inertia(inv_inertia2, ori2, n);
        float w1 = scripted_obj1 ? 0.0 : dot(obj1_world_inv_i_n, n);
        float w2 = scripted_obj2 ? 0.0 : dot(obj2_world_inv_i_n, n);
        float denom = w1 + w2 + tilde_alpha;

        if (denom > kJointSolveEpsilon) {
            float dlambda = (angular_norm - fixed_rotation_lagrange.v[jidx] * tilde_alpha) / denom;

            if (!scripted_obj1) {
                ang1 += obj1_world_inv_i_n * dlambda;
            }
            if (!scripted_obj2) {
                ang2 += -(obj2_world_inv_i_n * dlambda);
            }

            fixed_rotation_lagrange.v[jidx] += dlambda;
        }
    }

    // =====================================================================
    // Constraint 2: Position constraint
    //
    // Use the midpoint of the two COMs as a symmetric attachment point.
    // This gives non-zero lever arms on BOTH bodies, allowing the position
    // constraint to transmit torque to both obj1 and obj2.
    // =====================================================================
    vec3 anchor_obj1_local = joint.initial_rel_pos_local.xyz * 0.5;
    vec3 anchor_obj2_local =
        quat_rotate(quat_inverse(joint.initial_rel_rotation), -joint.initial_rel_pos_local.xyz * 0.5);

    vec3 r1 = quat_rotate(ori1, anchor_obj1_local);
    vec3 r2 = quat_rotate(ori2, anchor_obj2_local);

    vec3 attach1_world = pos1 + r1;
    vec3 attach2_world = pos2 + r2;

    vec3 positional_correction = attach2_world - attach1_world;
    float positional_norm = length(positional_correction);

    if (positional_norm > kJointSolveEpsilon) {
        vec3 n = positional_correction / positional_norm;

        vec3 r1_n = cross(r1, n);
        vec3 r2_n = cross(r2, n);
        vec3 i1_r1n = apply_world_inv_inertia(inv_inertia1, ori1, r1_n);
        vec3 i2_r2n = apply_world_inv_inertia(inv_inertia2, ori2, r2_n);
        float w1 = inv_mass_obj1 + (scripted_obj1 ? 0.0 : dot(i1_r1n, r1_n));
        float w2 = inv_mass_obj2 + (scripted_obj2 ? 0.0 : dot(i2_r2n, r2_n));
        float denom = w1 + w2 + tilde_alpha;

        if (denom > kJointSolveEpsilon) {
            float dlambda = (positional_norm - fixed_position_lagrange.v[jidx] * tilde_alpha) / denom;
            vec3 impulse = dlambda * n;

            if (!scripted_obj1) {
                lin1 += inv_mass_obj1 * impulse;
                ang1 += apply_world_inv_inertia(inv_inertia1, ori1, cross(r1, impulse));
            }

            if (!scripted_obj2) {
                lin2 += -(inv_mass_obj2 * impulse);
                ang2 += -apply_world_inv_inertia(inv_inertia2, ori2, cross(r2, impulse));
            }

            fixed_position_lagrange.v[jidx] += dlambda;
        }
    }

    joint_add_body_correction(obj1_idx, lin1, ang1);
    joint_add_body_correction(obj2_idx, lin2, ang2);
}
#endif // XPBD_JOINT_SOLVE_FIXED

#endif // XPBD_JOINT_SOLVE_GLSL
