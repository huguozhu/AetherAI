module;
#include "d3d12_common.h"
module aether.rhi;

#include "resource_d3d12_impl.h"
#include "command_list_d3d12_impl.h"
#include "pipeline_d3d12_impl.h"
#include "device_d3d12_impl.h"

import aether.core;
import <utility>;
import <memory>;
import <vector>;

namespace aether::rhi {

// === GraphicsCommandListD3D12 ===

GraphicsCommandListD3D12::GraphicsCommandListD3D12(ID3D12Device10* device)
    : m_device(device) {}

void GraphicsCommandListD3D12::reset() {
    HRESULT hr1 = m_allocator->Reset();
    HRESULT hr2 = m_list->Reset(m_allocator.Get(), nullptr);
    if (FAILED(hr1)) aether::log::error("reset: allocator Reset failed hr=0x{:08X}", (unsigned)hr1);
    if (FAILED(hr2)) aether::log::error("reset: list Reset failed hr=0x{:08X}", (unsigned)hr2);
}

void GraphicsCommandListD3D12::close() {
    HRESULT hr = m_list->Close();
    if (FAILED(hr)) aether::log::error("close: list Close failed hr=0x{:08X}", (unsigned)hr);
}

void GraphicsCommandListD3D12::resource_barrier(Resource* resource,
                                                  ResourceState stateBefore,
                                                  ResourceState stateAfter) {
    auto* buffer = dynamic_cast<BufferD3D12*>(resource);
    auto* texture = dynamic_cast<TextureD3D12*>(resource);
    ID3D12Resource* res = buffer ? buffer->resource.Get()
                       : texture ? texture->resource.Get()
                       : nullptr;
    if (!res) {
        aether::log::warn("resource_barrier: no res (buffer={} texture={})",
                          (void*)buffer, (void*)texture);
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = res;
    barrier.Transition.StateBefore = to_d3d12_resource_state(stateBefore);
    barrier.Transition.StateAfter = to_d3d12_resource_state(stateAfter);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_list->ResourceBarrier(1, &barrier);
}

void GraphicsCommandListD3D12::clear_texture(Texture* texture,
                                               const float color[4]) {
    auto* tex = dynamic_cast<TextureD3D12*>(texture);
    if (!tex || !tex->hasRtvHandle) {
        if (!tex) aether::log::warn("clear_texture: dynamic_cast failed");
        return;
    }
    m_list->ClearRenderTargetView(tex->rtvHandle, color, 0, nullptr);
}

void GraphicsCommandListD3D12::bind_render_targets(Texture* rtv, Texture* dsv) {
    auto* rt = static_cast<TextureD3D12*>(rtv);
    if (!rt || !rt->hasRtvHandle) return;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    if (dsv) {
        auto* ds = static_cast<TextureD3D12*>(dsv);
        if (ds && ds->hasDsvHandle) dsvHandle = ds->dsvHandle;
    }
    m_list->OMSetRenderTargets(1, &rt->rtvHandle, FALSE,
                                dsvHandle.ptr ? &dsvHandle : nullptr);
}

void GraphicsCommandListD3D12::clear_depth(Texture* texture,
                                            float depth,
                                            uint8_t stencil) {
    auto* tex = dynamic_cast<TextureD3D12*>(texture);
    if (!tex || !tex->hasDsvHandle) return;
    m_list->ClearDepthStencilView(tex->dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
}

void GraphicsCommandListD3D12::bind_pipeline(PipelineState* pipeline) {
    if (m_rootSignature) {
        m_list->SetGraphicsRootSignature(m_rootSignature);
    }
    auto* gfxPipeline = dynamic_cast<GraphicsPipelineD3D12*>(pipeline);
    if (gfxPipeline) {
        m_list->SetPipelineState(gfxPipeline->pso.Get());
        m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    } else {
        aether::log::warn("bind_pipeline: dynamic_cast to GraphicsPipelineD3D12 failed");
    }
}

void GraphicsCommandListD3D12::bind_descriptor(uint32_t /*slot*/,
                                                ShaderBinding* binding) {
    auto* sb = dynamic_cast<ShaderBindingD3D12*>(binding);
    if (sb && m_descriptorHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
        m_list->SetDescriptorHeaps(1, heaps);
        // Root signature must have a descriptor table at param 0 for this to work.
        // The current default root signature has 0 params; calling this will fail.
        // TODO: switch to a root signature with descriptor table
    }
}

void GraphicsCommandListD3D12::set_viewport(float x, float y, float w, float h,
                                             float minDepth, float maxDepth) {
    D3D12_VIEWPORT vp{x, y, w, h, minDepth, maxDepth};
    m_list->RSSetViewports(1, &vp);
}

void GraphicsCommandListD3D12::set_scissor(int32_t left, int32_t top,
                                            int32_t right, int32_t bottom) {
    D3D12_RECT rect{left, top, right, bottom};
    m_list->RSSetScissorRects(1, &rect);
}

void GraphicsCommandListD3D12::ia_set_vertex_buffer(uint32_t slot,
                                                     Buffer* buffer,
                                                     uint32_t stride) {
    auto* buf = static_cast<BufferD3D12*>(buffer);
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = buf->resource->GetGPUVirtualAddress();
    vbv.SizeInBytes = static_cast<UINT>(buf->desc.size);
    vbv.StrideInBytes = stride;
    m_list->IASetVertexBuffers(slot, 1, &vbv);
    m_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void GraphicsCommandListD3D12::ia_set_index_buffer(Buffer* buffer,
                                                    Format format) {
    auto* buf = static_cast<BufferD3D12*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv{};
    ibv.BufferLocation = buf->resource->GetGPUVirtualAddress();
    ibv.SizeInBytes = static_cast<UINT>(buf->desc.size);
    ibv.Format = (format == Format::R32_FLOAT) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    m_list->IASetIndexBuffer(&ibv);
}

void GraphicsCommandListD3D12::draw(uint32_t vertexCount, uint32_t instanceCount,
                                     uint32_t startVertex, uint32_t startInstance) {
    m_list->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void GraphicsCommandListD3D12::draw_indexed(uint32_t indexCount,
                                             uint32_t instanceCount,
                                             uint32_t startIndex,
                                             int32_t baseVertex,
                                             uint32_t startInstance) {
    m_list->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void GraphicsCommandListD3D12::draw_indirect(Buffer* /*args*/,
                                              uint32_t /*offset*/,
                                              uint32_t /*drawCount*/,
                                              uint32_t /*stride*/) {
    // TODO: requires command signature
}

void GraphicsCommandListD3D12::dispatch_mesh(uint32_t groupX, uint32_t groupY,
                                              uint32_t groupZ) {
    m_list->DispatchMesh(groupX, groupY, groupZ);
}

void GraphicsCommandListD3D12::dispatch_rays(const void* /*rayGenShaderTable*/,
                                              const void* /*missShaderTable*/,
                                              const void* /*hitGroupTable*/,
                                              uint32_t /*width*/, uint32_t /*height*/,
                                              uint32_t /*depth*/) {
    // TODO: requires ray tracing pipeline state object
}

// === ComputeCommandListD3D12 ===

ComputeCommandListD3D12::ComputeCommandListD3D12(ID3D12Device10* device)
    : m_device(device) {}

void ComputeCommandListD3D12::reset() {
    HRESULT hr1 = m_allocator->Reset();
    HRESULT hr2 = m_list->Reset(m_allocator.Get(), nullptr);
    if (FAILED(hr1)) aether::log::error("compute reset: allocator Reset failed hr=0x{:08X}", (unsigned)hr1);
    if (FAILED(hr2)) aether::log::error("compute reset: list Reset failed hr=0x{:08X}", (unsigned)hr2);
}

void ComputeCommandListD3D12::close() {
    HRESULT hr = m_list->Close();
    if (FAILED(hr)) aether::log::error("compute close: list Close failed hr=0x{:08X}", (unsigned)hr);
}

void ComputeCommandListD3D12::resource_barrier(Resource* resource,
                                                ResourceState stateBefore,
                                                ResourceState stateAfter) {
    auto* buffer = dynamic_cast<BufferD3D12*>(resource);
    auto* texture = dynamic_cast<TextureD3D12*>(resource);
    ID3D12Resource* res = buffer ? buffer->resource.Get()
                       : texture ? texture->resource.Get()
                       : nullptr;
    if (!res) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = res;
    barrier.Transition.StateBefore = to_d3d12_resource_state(stateBefore);
    barrier.Transition.StateAfter = to_d3d12_resource_state(stateAfter);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_list->ResourceBarrier(1, &barrier);
}

void ComputeCommandListD3D12::clear_texture(Texture* /*texture*/,
                                             const float /*color*/[4]) {}

void ComputeCommandListD3D12::clear_depth(Texture* /*texture*/,
                                           float /*depth*/,
                                           uint8_t /*stencil*/) {}

void ComputeCommandListD3D12::bind_pipeline(PipelineState* /*pipeline*/) {}

void ComputeCommandListD3D12::bind_descriptor(uint32_t /*slot*/,
                                              ShaderBinding* binding) {
    auto* sb = dynamic_cast<ShaderBindingD3D12*>(binding);
    if (sb && m_descriptorHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
        m_list->SetDescriptorHeaps(1, heaps);
        // Root signature must have a descriptor table at param 0 for this to work.
        // The current default root signature has 0 params.
        // TODO: switch to a root signature with descriptor table
    }
}

void ComputeCommandListD3D12::dispatch(uint32_t groupX, uint32_t groupY,
                                       uint32_t groupZ) {
    m_list->Dispatch(groupX, groupY, groupZ);
}

void ComputeCommandListD3D12::dispatch_indirect(Buffer* /*args*/,
                                                 uint32_t /*offset*/) {
    // TODO: requires command signature
}

// === CopyCommandListD3D12 ===

CopyCommandListD3D12::CopyCommandListD3D12(ID3D12Device10* device)
    : m_device(device) {}

void CopyCommandListD3D12::reset() {
    HRESULT hr1 = m_allocator->Reset();
    HRESULT hr2 = m_list->Reset(m_allocator.Get(), nullptr);
    if (FAILED(hr1)) aether::log::error("copy reset: allocator Reset failed hr=0x{:08X}", (unsigned)hr1);
    if (FAILED(hr2)) aether::log::error("copy reset: list Reset failed hr=0x{:08X}", (unsigned)hr2);
    m_pendingUploads.clear();
}

void CopyCommandListD3D12::close() {
    HRESULT hr = m_list->Close();
    if (FAILED(hr)) aether::log::error("copy close: list Close failed hr=0x{:08X}", (unsigned)hr);
}

void CopyCommandListD3D12::resource_barrier(Resource* resource,
                                             ResourceState stateBefore,
                                             ResourceState stateAfter) {
    auto* buffer = dynamic_cast<BufferD3D12*>(resource);
    auto* texture = dynamic_cast<TextureD3D12*>(resource);
    ID3D12Resource* res = buffer ? buffer->resource.Get()
                       : texture ? texture->resource.Get()
                       : nullptr;
    if (!res) return;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = res;
    barrier.Transition.StateBefore = to_d3d12_resource_state(stateBefore);
    barrier.Transition.StateAfter = to_d3d12_resource_state(stateAfter);
    m_list->ResourceBarrier(1, &barrier);
}

void CopyCommandListD3D12::clear_texture(Texture* /*texture*/,
                                          const float /*color*/[4]) {}

void CopyCommandListD3D12::clear_depth(Texture* /*texture*/,
                                        float /*depth*/,
                                        uint8_t /*stencil*/) {}

void CopyCommandListD3D12::bind_pipeline(PipelineState* /*pipeline*/) {}

void CopyCommandListD3D12::bind_descriptor(uint32_t /*slot*/,
                                           ShaderBinding* /*binding*/) {}

void CopyCommandListD3D12::upload_buffer(Buffer* dst, uint64_t dstOffset,
                                          const void* src, size_t size) {
    if (!src || size == 0) {
        aether::log::error("upload_buffer: invalid src or size");
        return;
    }
    auto* dstBuf = static_cast<BufferD3D12*>(dst);
    if (!dstBuf || !dstBuf->resource) {
        aether::log::error("upload_buffer: invalid dst buffer");
        return;
    }

    // Transition dst to COPY_DEST
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dstBuf->resource.Get();
    barrier.Transition.StateBefore = dstBuf->initialState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_list->ResourceBarrier(1, &barrier);

    // Create a temporary upload buffer and copy data into it
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = size;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadRes;
    HRESULT hr = m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadRes));
    if (FAILED(hr)) {
        aether::log::error("upload_buffer: failed to create upload buffer");
        return;
    }

    // Map and copy source data into upload buffer
    void* mapped;
    uploadRes->Map(0, nullptr, &mapped);
    memcpy(mapped, src, size);
    uploadRes->Unmap(0, nullptr);

    // Issue GPU copy
    m_list->CopyBufferRegion(dstBuf->resource.Get(), dstOffset,
                              uploadRes.Get(), 0, size);

    // Transition dst back to original state
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    m_list->ResourceBarrier(1, &barrier);

    // Keep upload resource alive until command list is submitted and GPU is done
    m_pendingUploads.push_back(std::move(uploadRes));
}

void CopyCommandListD3D12::upload_texture(Texture* dst,
                                           const void* src,
                                           size_t size) {
    auto* dstTex = dynamic_cast<TextureD3D12*>(dst);
    if (!dstTex || !dstTex->resource || !src || size == 0) {
        aether::log::error("upload_texture: invalid dst or src");
        return;
    }

    // Transition dst to COPY_DEST
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = dstTex->resource.Get();
    barrier.Transition.StateBefore = dstTex->initialState;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_list->ResourceBarrier(1, &barrier);

    // Get copyable footprint from the D3D12 resource desc
    auto resDesc = dstTex->resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints(&resDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes);

    // Create staging upload buffer
    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc{};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = totalBytes;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadRes;
    HRESULT hr = m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadRes));
    if (FAILED(hr)) {
        aether::log::error("upload_texture: failed to create upload buffer");
        return;
    }

    // Map and copy row-by-row with pitch alignment
    void* mapped;
    uploadRes->Map(0, nullptr, &mapped);
    uint8_t* dstData = static_cast<uint8_t*>(mapped) + footprint.Offset;
    const uint8_t* srcData = static_cast<const uint8_t*>(src);
    UINT srcRowPitch = static_cast<UINT>(size / numRows);
    UINT copyBytes = static_cast<UINT>((std::min)(static_cast<UINT64>(srcRowPitch), rowSizeBytes));
    for (UINT r = 0; r < numRows; ++r) {
        memcpy(dstData + r * footprint.Footprint.RowPitch,
               srcData + r * srcRowPitch, copyBytes);
    }
    uploadRes->Unmap(0, nullptr);

    // Issue GPU copy via CopyTextureRegion
    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = dstTex->resource.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = uploadRes.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    m_list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Transition dst back to original state
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    m_list->ResourceBarrier(1, &barrier);

    m_pendingUploads.push_back(std::move(uploadRes));
}

} // namespace aether::rhi
