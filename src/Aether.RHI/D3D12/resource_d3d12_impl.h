#pragma once
#include "d3d12_common.h"

namespace aether::rhi {

// Forward declarations from module interface
class Buffer;
class Texture;
class Fence;

// === BufferD3D12 ===
struct BufferD3D12 : public Buffer {
    ComPtr<ID3D12Resource> resource;
    BufferDesc desc;
    D3D12_RESOURCE_STATES initialState;

    BufferDesc get_desc() const override;
    uint64_t get_gpu_address() const override;
    void* map() override;
    void unmap() override;
};

// === TextureD3D12 ===
struct TextureD3D12 : public Texture {
    ComPtr<ID3D12Resource> resource;
    TextureDesc desc;
    D3D12_RESOURCE_STATES initialState;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
    bool hasRtvHandle = false;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    bool hasDsvHandle = false;

    TextureDesc get_desc() const override;
    uint64_t get_gpu_address() const override;
};

// === FenceD3D12 ===
struct FenceD3D12 : public Fence {
    ComPtr<ID3D12Fence> fence;
    ID3D12CommandQueue* queue; // non-owning
    HANDLE event;

    FenceD3D12(ID3D12Fence* f, ID3D12CommandQueue* q);
    ~FenceD3D12() override;

    uint64_t get_value() const override;
    void signal(uint64_t value) override;
    void wait(uint64_t value) override;
};

} // namespace aether::rhi
