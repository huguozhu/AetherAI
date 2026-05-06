module;
#include "d3d12_common.h"
module aether.rhi;

import aether.core;
import <utility>;
import <memory>;

#include "resource_d3d12_impl.h"

namespace aether::rhi {

// === BufferD3D12 ===
BufferDesc BufferD3D12::get_desc() const {
    return desc;
}

uint64_t BufferD3D12::get_gpu_address() const {
    return resource->GetGPUVirtualAddress();
}

void* BufferD3D12::map() {
    void* data;
    resource->Map(0, nullptr, &data);
    return data;
}

void BufferD3D12::unmap() {
    resource->Unmap(0, nullptr);
}

// === TextureD3D12 ===
TextureDesc TextureD3D12::get_desc() const {
    return desc;
}

uint64_t TextureD3D12::get_gpu_address() const {
    return resource->GetGPUVirtualAddress();
}

// === FenceD3D12 ===
FenceD3D12::FenceD3D12(ID3D12Fence* f, ID3D12CommandQueue* q) : fence(f), queue(q) {
    event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

FenceD3D12::~FenceD3D12() {
    if (event) CloseHandle(event);
}

uint64_t FenceD3D12::get_value() const {
    return fence->GetCompletedValue();
}

void FenceD3D12::signal(uint64_t value) {
    queue->Signal(fence.Get(), value);
}

void FenceD3D12::wait(uint64_t value) {
    fence->SetEventOnCompletion(value, event);
    WaitForSingleObject(event, INFINITE);
}

} // namespace aether::rhi
