#ifndef FRAMEWORK_BRIDGE_INTERNAL_JOINTCONVERTER_INCLUDED
#define FRAMEWORK_BRIDGE_INTERNAL_JOINTCONVERTER_INCLUDED

#include "../PhysicsDescriptors.h"
#include <Physics/PhysicsDescriptors.h>

#include <glm.hpp>
#include <gtc/quaternion.hpp>

namespace Engine {
    namespace detail {

        /**
         * @brief Pure-function module that converts GO-space joint submit data
         *        to COM-space GPU-ready joint structures.
         *
         * COM offset vectors c1 and c2 (in GO-local space) are applied to
         * transform GO-local positions into COM-local positions.
         */
        class JointConverter {
        public:
            /**
             * @brief Convert fixed joint submit data to a GPU-ready FixedJointComDescriptor.
             *
             * Formula: com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1
             *
             * @param data The GO-space fixed joint submit data.
             * @param c1   COM offset of obj1 in GO-local space.
             * @param c2   COM offset of obj2 in GO-local space.
             * @return COM-space fixed joint with COM-local relative transform.
             */
            static FixedJointComDescriptor ConvertFixed(
                const FixedJointSubmitData &data, const glm::vec3 &c1, const glm::vec3 &c2
            ) {
                FixedJointComDescriptor joint{};
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

            /**
             * @brief Convert hinge joint submit data to a GPU-ready HingeJointComDescriptor.
             *
             * Formulas: anchor_com = anchor_go - c1; com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1.
             * The hinge axis is a direction vector unaffected by COM offset translation.
             *
             * @param data The GO-space hinge joint submit data.
             * @param c1   COM offset of obj1 in GO-local space.
             * @param c2   COM offset of obj2 in GO-local space.
             * @return COM-space hinge joint with COM-local anchor point and relative transform.
             */
            static HingeJointComDescriptor ConvertHinge(
                const HingeJointSubmitData &data, const glm::vec3 &c1, const glm::vec3 &c2
            ) {
                HingeJointComDescriptor joint{};
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

#endif // FRAMEWORK_BRIDGE_INTERNAL_JOINTCONVERTER_INCLUDED
