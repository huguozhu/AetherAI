module;
#include <memory>
#include <utility>

module aether.renderer;

import aether.core;
import aether.rhi;

namespace aether::renderer {

RenderScene::RenderScene(std::shared_ptr<rhi::Device> device)
    : m_device(std::move(device))
{
    // Default scene bounds — large enough for most scenes
    math::BoundingBox defaultBounds{{-500, -500, -500}, {500, 500, 500}};
    m_sceneManager = std::make_unique<OctreeSceneManager>(
        m_device, defaultBounds);
    log::info("RenderScene: initialized with OctreeSceneManager");
}

RenderScene::RenderScene(std::shared_ptr<rhi::Device> device,
                          std::unique_ptr<SceneManager> sceneManager)
    : m_device(std::move(device))
    , m_sceneManager(std::move(sceneManager))
{
    log::info("RenderScene: initialized with custom SceneManager");
}

uint32_t RenderScene::add_object(const SceneObjectGPU& obj) {
    return m_sceneManager ? m_sceneManager->add_object(obj) : UINT32_MAX;
}

uint32_t RenderScene::register_component(std::shared_ptr<Component> component) {
    return m_sceneManager ? m_sceneManager->register_component(std::move(component)) : UINT32_MAX;
}

void RenderScene::unregister_component(uint32_t componentId) {
    if (m_sceneManager) m_sceneManager->unregister_component(componentId);
}

std::shared_ptr<Component> RenderScene::get_component(uint32_t componentId) const {
    return m_sceneManager ? m_sceneManager->get_component(componentId) : nullptr;
}

void RenderScene::update(const SceneView& view) {
    if (m_sceneManager) {
        m_sceneManager->set_view(view);
    }
}

void RenderScene::render(rhi::GraphicsCommandList* gfxCmd,
                          rhi::ComputeCommandList* computeCmd,
                          rhi::CopyCommandList* copyCmd)
{
    if (m_sceneManager) {
        m_sceneManager->render(gfxCmd, computeCmd, copyCmd);
    }
}

void RenderScene::render_forward(rhi::GraphicsCommandList* gfxCmd,
                                  rhi::GraphicsPipeline* pipeline) {
    if (m_sceneManager) {
        m_sceneManager->render_forward(gfxCmd, pipeline);
    }
}

} // namespace aether::renderer
