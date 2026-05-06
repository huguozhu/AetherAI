#pragma once
#include "d3d12_common.h"

namespace aether::rhi {

// Forward declarations from module interface
class GraphicsCommandList;
class ComputeCommandList;
class CopyCommandList;

// === GraphicsCommandListD3D12 ===
struct GraphicsCommandListD3D12 : public GraphicsCommandList {
    ComPtr<ID3D12GraphicsCommandList6> m_list;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ID3D12Device10* m_device;
    ID3D12RootSignature* m_rootSignature = nullptr;
    UINT m_allocatorSlot = UINT_MAX;
    ID3D12DescriptorHeap* m_descriptorHeap = nullptr;

    GraphicsCommandListD3D12(ID3D12Device10* device);

    void reset() override;
    void close() override;
    void resource_barrier(Resource* resource, ResourceState stateBefore, ResourceState stateAfter) override;
    void clear_texture(Texture* texture, const float color[4]) override;
    void clear_depth(Texture* texture, float depth, uint8_t stencil) override;
    void bind_pipeline(PipelineState* pipeline) override;
    void bind_descriptor(uint32_t slot, ShaderBinding* binding) override;
    void set_viewport(float x, float y, float w, float h, float minDepth, float maxDepth) override;
    void set_scissor(int32_t left, int32_t top, int32_t right, int32_t bottom) override;
    void ia_set_vertex_buffer(uint32_t slot, Buffer* buffer, uint32_t stride) override;
    void ia_set_index_buffer(Buffer* buffer, Format format) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t startVertex, uint32_t startInstance) override;
    void draw_indexed(uint32_t indexCount, uint32_t instanceCount,
                      uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override;
    void draw_indirect(Buffer* args, uint32_t offset, uint32_t drawCount, uint32_t stride) override;
    void bind_render_targets(Texture* rtv, Texture* dsv = nullptr) override;
    void dispatch_mesh(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
    void dispatch_rays(const void* rayGenShaderTable, const void* missShaderTable,
                       const void* hitGroupTable, uint32_t width, uint32_t height, uint32_t depth) override;
};

// === ComputeCommandListD3D12 ===
struct ComputeCommandListD3D12 : public ComputeCommandList {
    ComPtr<ID3D12GraphicsCommandList6> m_list;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ID3D12Device10* m_device;
    ID3D12RootSignature* m_rootSignature = nullptr;
    UINT m_allocatorSlot = UINT_MAX;
    ID3D12DescriptorHeap* m_descriptorHeap = nullptr;

    ComputeCommandListD3D12(ID3D12Device10* device);

    void reset() override;
    void close() override;
    void resource_barrier(Resource* resource, ResourceState stateBefore, ResourceState stateAfter) override;
    void clear_texture(Texture* texture, const float color[4]) override;
    void clear_depth(Texture* texture, float depth, uint8_t stencil) override;
    void bind_pipeline(PipelineState* pipeline) override;
    void bind_descriptor(uint32_t slot, ShaderBinding* binding) override;
    void dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
    void dispatch_indirect(Buffer* args, uint32_t offset) override;
};

// === CopyCommandListD3D12 ===
struct CopyCommandListD3D12 : public CopyCommandList {
    ComPtr<ID3D12GraphicsCommandList6> m_list;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ID3D12Device10* m_device;
    UINT m_allocatorSlot = UINT_MAX;
    std::vector<ComPtr<ID3D12Resource>> m_pendingUploads; // kept alive until submit

    CopyCommandListD3D12(ID3D12Device10* device);

    void reset() override;
    void close() override;
    void resource_barrier(Resource* resource, ResourceState stateBefore, ResourceState stateAfter) override;
    void clear_texture(Texture* texture, const float color[4]) override;
    void clear_depth(Texture* texture, float depth, uint8_t stencil) override;
    void bind_pipeline(PipelineState* pipeline) override;
    void bind_descriptor(uint32_t slot, ShaderBinding* binding) override;
    void upload_buffer(Buffer* dst, uint64_t dstOffset, const void* src, size_t size) override;
    void upload_texture(Texture* dst, const void* src, size_t size) override;
};

} // namespace aether::rhi
