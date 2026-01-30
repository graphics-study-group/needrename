#ifndef RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
#define RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H

#include "RenderGraphBuilder.h"

namespace Engine {
    class AssetRef;
    class ComputeStage;

    class ComplexRenderGraphBuilder : public RenderGraphBuilder {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        ComplexRenderGraphBuilder(RenderSystem &system);
        ~ComplexRenderGraphBuilder() = default;

        std::unique_ptr<RenderGraph> BuildDefaultRenderGraph(
            uint32_t texture_width,
            uint32_t texture_height,
            std::function<vk::Extent2D()> get_viewport_func,
            std::function<uint8_t()> get_camera_index_func,
            int32_t &final_color_target_id
        );

        std::unique_ptr<RenderGraph> BuildEditorRenderGraph(
            uint32_t texture_width,
            uint32_t texture_height,
            std::function<vk::Extent2D()> get_scene_widget_viewport_func,
            std::function<uint8_t()> get_scene_camera_index_func,
            std::function<vk::Extent2D()> get_game_widget_viewport_func,
            std::function<uint8_t()> get_game_camera_index_func,
            GUISystem * gui_system,
            int32_t &scene_widget_color_id,
            int32_t &game_widget_color_id,
            int32_t &final_color_target_id
        );

    protected:
        std::shared_ptr<AssetRef> m_bloom_shader{};

        void RecordMainRender(
            uint32_t texture_width,
            uint32_t texture_height,
            std::function<vk::Extent2D()> get_viewport_func,
            std::function<uint8_t()> get_camera_index_func,
            std::shared_ptr<ComputeStage> bloom_compute_stage,
            int32_t &hdr_color_id,
            int32_t &bloom_temp_id,
            int32_t &final_color_target_id
        );
    };
} // namespace Engine

#endif // RENDER_PIPELINE_RENDERGRAPH_COMPLEXGRAPHBUILDER_H
