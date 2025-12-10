#ifndef EXAMPLE_TOON_SHADING_UI_INCLUDED
#define EXAMPLE_TOON_SHADING_UI_INCLUDED

#include <Render/FullRenderSystem.h>
#include <UserInterface/GUISystem.h>

#include <glm.hpp>

namespace {
    glm::vec3 GetCartesian(float zenith, float azimuth) {
        return glm::vec3{- sin(zenith) * cos(azimuth), - sin(zenith) * sin(azimuth), - cos(zenith)};
    }
}

struct UIState {
    struct GlobalIllumination {
        float zenith, azimuth;
        float r, g, b;
    } illum;
} g_state;

void DrawGUI() {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::SetNextWindowPos({10, 10});
    ImGui::SetNextWindowSize(ImVec2{300, 300});
    ImGui::Begin("Configuration", nullptr, flags);
    ImGui::SliderAngle("Zenith", &g_state.illum.zenith, -180.0f, 180.0f);
    ImGui::SliderAngle("Azimuth", &g_state.illum.azimuth, 0.0f, 360.0f);
    ImGui::End();

    g_state.illum.r = g_state.illum.g = g_state.illum.b = 1.0f;
}

void UpdateSceneData (Engine::RenderSystem & system) {
    system.GetSceneDataManager().SetLightDirectionalNonShadowCasting(
        0,
        GetCartesian(g_state.illum.zenith, g_state.illum.azimuth),
        glm::vec3(g_state.illum.r, g_state.illum.g, g_state.illum.b)
    );
}

#endif // EXAMPLE_TOON_SHADING_UI_INCLUDED
