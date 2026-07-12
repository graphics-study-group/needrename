#ifndef FRAMEWORK_WORLD_PHYSICS_INTERNAL_JOINTCONVERTER_INCLUDED
#define FRAMEWORK_WORLD_PHYSICS_INTERNAL_JOINTCONVERTER_INCLUDED

#include "../PhysicsDescriptors.h"
#include <Physics/PhysicsScene.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

namespace Engine {
    namespace detail {

        class JointConverter {
        public:
            static GpuFixedJoint ConvertFixed(
                const FixedJointSubmitData &data, const glm::vec3 &c1, const glm::vec3 &c2
            ) {
                GpuFixedJoint joint{};
                joint.obj1_index = data.obj1_index;
                joint.obj2_index = data.obj2_index;
                joint.compliance = data.compliance;

                const glm::vec3 com_rel_pos = data.initial_rel_pos_local + data.initial_rel_rotation * c2 - c1;
                joint.initial_rel_pos_local = glm::vec4(com_rel_pos.x, com_rel_pos.y, com_rel_pos.z, 0.0f);
                joint.initial_rel_rotation = glm::vec4(
                    data.initial_rel_rotation.x,
                    data.initial_rel_rotation.y,
                    data.initial_rel_rotation.z,
                    data.initial_rel_rotation.w
                );

                return joint;
            }

            static GpuHingeJoint ConvertHinge(
                const HingeJointSubmitData &data, const glm::vec3 &c1, const glm::vec3 &c2
            ) {
                GpuHingeJoint joint{};
                joint.obj1_index = data.obj1_index;
                joint.obj2_index = data.obj2_index;
                joint.compliance = data.compliance;

                const glm::vec3 hinge_anchor_com = data.hinge_anchor_obj1 - c1;
                const glm::vec3 com_rel_pos = data.initial_rel_pos_local + data.initial_rel_rotation * c2 - c1;

                joint.hinge_axis_obj1 =
                    glm::vec4(data.hinge_axis_obj1.x, data.hinge_axis_obj1.y, data.hinge_axis_obj1.z, 0.0f);
                joint.hinge_anchor_obj1 = glm::vec4(hinge_anchor_com.x, hinge_anchor_com.y, hinge_anchor_com.z, 0.0f);
                joint.initial_rel_pos_local = glm::vec4(com_rel_pos.x, com_rel_pos.y, com_rel_pos.z, 0.0f);
                joint.initial_rel_rotation = glm::vec4(
                    data.initial_rel_rotation.x,
                    data.initial_rel_rotation.y,
                    data.initial_rel_rotation.z,
                    data.initial_rel_rotation.w
                );

                return joint;
            }
        };

    } // namespace detail
} // namespace Engine

#endif // FRAMEWORK_WORLD_PHYSICS_INTERNAL_JOINTCONVERTER_INCLUDED
