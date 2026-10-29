#include "LightComponentInspector.h"

#include <Framework/Component/RenderComponent/LightComponent.h>
#include <Framework/MainClass.h>
#include <Framework/Object/GameObject.h>
#include <Framework/World/WorldSystem.h>
#include <Render/RenderSystem/SceneDataManager.h>

#include <imgui.h>

namespace Editor {
    namespace {
        /// Colour of a label that says the light holds the role.
        constexpr ImVec4 HOLDER_LABEL_COLOR{0.45f, 0.85f, 0.45f, 1.0f};
        /// Colour of a warning: this light is flagged but does not cast.
        constexpr ImVec4 WARNING_COLOR{0.95f, 0.45f, 0.35f, 1.0f};
        /// Colour of a notice that points at another light.
        constexpr ImVec4 NOTICE_COLOR{0.90f, 0.75f, 0.35f, 1.0f};
        /// Colour of a notice that no shadow-casting light exists.
        constexpr ImVec4 INACTIVE_NOTICE_COLOR{0.6f, 0.6f, 0.6f, 1.0f};

        /**
         * @brief The light that currently holds the scene's shadow-caster role.
         *
         * @return The holder, or nullptr when no directional light casts shadows.
         */
        Engine::LightComponent *FindShadowHolder() {
            auto world = Engine::MainClass::GetInstance()->GetWorldSystem();
            return world != nullptr ? world->GetShadowCastingLight() : nullptr;
        }

        /// @brief GameObject name of a light, for the inspector's notices.
        const char *LightName(const Engine::LightComponent &light) {
            const auto *game_object = light.GetParentGameObject();
            return game_object != nullptr ? game_object->m_name.c_str() : "<unnamed light>";
        }
    } // namespace

    void LightComponentInspector::Inspect(Engine::Component &component) {
        auto &light = static_cast<Engine::LightComponent &>(component);

        ImGui::PushID("LightComponent");
        if (ImGui::TreeNodeEx("##header", ImGuiTreeNodeFlags_DefaultOpen, "<LightComponent>")) {
            int type = static_cast<int>(light.m_type);
            if (ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0")) {
                light.m_type = static_cast<Engine::LightType>(type);
            }
            ImGui::ColorEdit3("Color", &light.m_color[0]);
            ImGui::DragFloat("Intensity", &light.m_intensity, 0.05f, 0.0f, 1000.0f);

            // Each light's flag is its own: the editor never clears another light's
            // Cast Shadow, so an ambiguous scene stays visible instead of being
            // repaired behind the user's back. The role only exists for directional
            // lights, so the checkbox is not offered for the other types.
            if (light.m_type == Engine::LightType::Directional) {
                bool cast_shadow = light.m_cast_shadow;
                if (ImGui::Checkbox("Cast Shadow", &cast_shadow)) {
                    light.m_cast_shadow = cast_shadow;
                }
            } else {
                ImGui::TextDisabled("Only directional lights cast shadows.");
            }

            Engine::LightComponent *holder = FindShadowHolder();
            const bool is_holder = holder == &light;
            if (is_holder) {
                ImGui::TextColored(HOLDER_LABEL_COLOR, "This light is the scene's shadow-casting light.");
            } else if (holder != nullptr && light.m_cast_shadow) {
                // The one-line warning for a flagged light that loses the role.
                ImGui::TextColored(
                    WARNING_COLOR,
                    "Cast Shadow has no effect here: \"%s\" holds the shadow-caster role (it was created earlier).",
                    LightName(*holder)
                );
            } else if (holder != nullptr) {
                ImGui::TextColored(NOTICE_COLOR, "Shadows are cast by \"%s\".", LightName(*holder));
            } else {
                ImGui::TextColored(INACTIVE_NOTICE_COLOR, "No directional light in this scene casts shadows.");
            }

            // Shadow settings are editable on the holder only. A non-holder keeps
            // its values, which apply again when the role comes back.
            ImGui::BeginDisabled(!is_holder);
            ImGui::DragFloat("Shadow distance", &light.m_shadow_distance, 0.5f, 0.001f, 10000.0f);
            int cascade_count = static_cast<int>(light.m_cascade_count);
            if (ImGui::SliderInt(
                    "Cascade count",
                    &cascade_count,
                    1,
                    static_cast<int>(Engine::RenderSystemState::SceneDataManager::MAX_SHADOW_CASCADES)
                )) {
                light.m_cascade_count = static_cast<uint32_t>(cascade_count);
            }
            ImGui::EndDisabled();

            ImGui::TreePop();
        }
        ImGui::PopID();
    }
} // namespace Editor
