#include "PhysicsConstraintComponentInspector.h"
#include <Framework/Component/physics/PhysicsConstraintComponent.h>
#include <Framework/Object/GameObject.h>
#include <Framework/World/Scene.h>
#include <imgui.h>
#include <variant>

namespace Editor {
    void PhysicsConstraintComponentInspector::Inspect(Engine::Component &component) {
        auto &pcc = static_cast<Engine::PhysicsConstraintComponent &>(component);

        ImGui::PushID("PhysicsConstraintComponent");
        if (ImGui::TreeNodeEx(
                "##header",
                ImGuiTreeNodeFlags_DefaultOpen,
                "<PhysicsConstraintComponent> (%zu joints)",
                pcc.m_joints.size()
            )) {

            if (ImGui::Button("+ FixedJoint")) {
                pcc.m_joints.push_back(Engine::FixedJointDef{});
            }
            ImGui::SameLine();
            if (ImGui::Button("+ HingeJoint")) {
                pcc.m_joints.push_back(Engine::HingeJointDef{});
            }

            for (size_t i = 0; i < pcc.m_joints.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));

                auto &joint_var = pcc.m_joints[i];
                bool is_hinge = std::holds_alternative<Engine::HingeJointDef>(joint_var);
                const char *joint_type_label = is_hinge ? "HingeJoint" : "FixedJoint";

                bool remove_clicked = false;

                bool joint_open =
                    ImGui::TreeNodeEx("##joint", ImGuiTreeNodeFlags_DefaultOpen, "[%zu] %s", i, joint_type_label);

                if (joint_open) {
                    if (ImGui::Button("Remove")) {
                        pcc.m_joints.erase(pcc.m_joints.begin() + static_cast<ptrdiff_t>(i));
                        remove_clicked = true;
                    }

                    if (!remove_clicked) {
                        if (is_hinge) {
                            auto &hinge = std::get<Engine::HingeJointDef>(joint_var);
                            InspectObjectHandle("Obj2", hinge.m_obj2_handle, component);
                            ImGui::DragFloat3("Hinge Axis (Obj1)", &hinge.m_hinge_axis_obj1[0], 0.1f);
                            ImGui::DragFloat3("Hinge Anchor (Obj1)", &hinge.m_hinge_anchor_obj1[0], 0.1f);
                            ImGui::DragFloat("Compliance", &hinge.m_compliance, 0.01f, 0.0f, 1.0f);
                        } else {
                            auto &fixed = std::get<Engine::FixedJointDef>(joint_var);
                            InspectObjectHandle("Obj2", fixed.m_obj2_handle, component);
                            ImGui::DragFloat("Compliance", &fixed.m_compliance, 0.01f, 0.0f, 1.0f);
                        }
                    }

                    ImGui::TreePop();
                }

                ImGui::PopID();
                if (remove_clicked) {
                    break;
                }
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void PhysicsConstraintComponentInspector::InspectObjectHandle(
        const char *label, Engine::ObjectHandle &handle, Engine::Component &component
    ) {
        ImGui::Text("%s:", label);
        ImGui::SameLine();
        if (handle.IsValid()) {
            auto *scene = component.GetScene();
            if (scene != nullptr) {
                auto *go = scene->GetGameObject(handle);
                if (go != nullptr) {
                    ImGui::Text("%s##%u", go->m_name.c_str(), handle.GetID());
                } else {
                    ImGui::Text("Invalid (ID: %u)", handle.GetID());
                }
            } else {
                ImGui::Text("Handle ID: %u", handle.GetID());
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "None");
        }
    }
} // namespace Editor
