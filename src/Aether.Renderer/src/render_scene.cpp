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
    m_sceneManager = std::make_unique<GPUSceneManager>(m_device);

    m_cullingJob = std::make_unique<CullingJob>(
        m_device,
        m_sceneManager->scene_buffer_shared(),
        m_sceneManager->visible_buffer_shared(),
        kMaxObjects);

    m_indirectDraw = std::make_unique<IndirectDrawManager>(
        m_device,
        m_sceneManager->visible_buffer_shared(),
        m_sceneManager->indirect_buffer_shared());

    log::info("RenderScene: initialized");
}

uint32_t RenderScene::add_object(const SceneObjectGPU& obj) {
    return m_sceneManager->add_object(obj);
}

void RenderScene::update(const SceneView& view) {
    m_cullingJob->update_view(view);
}

void RenderScene::render(rhi::GraphicsCommandList* gfxCmd,
                          rhi::ComputeCommandList* computeCmd,
                          rhi::CopyCommandList* copyCmd)
{
    if (!gfxCmd || !computeCmd || !copyCmd) return;

    // 1. Upload scene data (if dirty)
    m_sceneManager->upload_scene_data(copyCmd);

    // 2. Dispatch culling compute shader
    m_cullingJob->dispatch(computeCmd);

    // 3. Dispatch compaction compute shader
    m_indirectDraw->dispatch_compact(computeCmd);

    // 4. Indirect draw
    m_indirectDraw->draw(gfxCmd, nullptr);

    log::debug("RenderScene: frame rendered ({} objects)",
               m_sceneManager->object_count());
}

} // namespace aether::renderer
