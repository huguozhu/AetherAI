# P0: RHI Core Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Fix D3D12 allocator lifecycle, implement descriptor heap + ShaderBinding system, and fix FenceD3D12::signal()

**Architecture:** Three independent changes: (1) ring buffer for compute/copy allocators (same pattern as graphics), (2) global descriptor heap with bump allocator + ShaderBindingD3D12, (3) wire FenceD3D12::signal() to Queue::Signal

**Tech Stack:** C++20, D3D12, CMake

---

## File Structure

### Modified files:
- `src/Aether.RHI/D3D12/device_d3d12_impl.h` — ring buffer fields, descriptor heap fields, BindingLayout change
- `src/Aether.RHI/D3D12/command_list_d3d12_impl.h` — m_allocatorSlot on Compute/Copy
- `src/Aether.RHI/D3D12/device_d3d12.cpp` — ring buffer logic, descriptor heap init, create_shader_binding, fence fix
- `src/Aether.RHI/D3D12/command_list_d3d12.cpp` — bind_descriptor() implementation
- `src/Aether.RHI/D3D12/resource_d3d12.cpp` — FenceD3D12 constructor change
- `src/Aether.RHI/D3D12/resource_d3d12_impl.h` — FenceD3D12 add queue pointer
- `src/Aether.RHI/src/aether.rhi.cppm` — BindingLayout add numDescriptors

### No new files needed.

---

### Task 1: Compute/Copy Allocator Ring Buffer

**Files:**
- Modify: `src/Aether.RHI/D3D12/command_list_d3d12_impl.h`
- Modify: `src/Aether.RHI/D3D12/device_d3d12_impl.h`
- Modify: `src/Aether.RHI/D3D12/device_d3d12.cpp`

- [ ] **Step 1: Add m_allocatorSlot to ComputeCommandListD3D12 and CopyCommandListD3D12**

In `command_list_d3d12_impl.h`, add `UINT m_allocatorSlot = UINT_MAX;` to both ComputeCommandListD3D12 and CopyCommandListD3D12:

```cpp
struct ComputeCommandListD3D12 : public ComputeCommandList {
    ComPtr<ID3D12GraphicsCommandList6> m_list;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ID3D12Device10* m_device;
    UINT m_allocatorSlot = UINT_MAX;
    // ...
};

struct CopyCommandListD3D12 : public CopyCommandList {
    ComPtr<ID3D12GraphicsCommandList6> m_list;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ID3D12Device10* m_device;
    UINT m_allocatorSlot = UINT_MAX;
    // ...
};
```

- [ ] **Step 2: Add ring buffer fields to DeviceD3D12**

In `device_d3d12_impl.h`, after the graphics ring buffer fields, add:

```cpp
    // Ring buffer for compute/copy command allocator recycling
    ComPtr<ID3D12CommandAllocator> m_cmdAllocatorsCompute[kMaxFrameLatency];
    ComPtr<ID3D12CommandAllocator> m_cmdAllocatorsCopy[kMaxFrameLatency];
    uint64_t                 m_cmdAllocatorFenceValuesCompute[kMaxFrameLatency] = {};
    uint64_t                 m_cmdAllocatorFenceValuesCopy[kMaxFrameLatency] = {};
    UINT                     m_currentAllocatorSlotCompute = 0;
    UINT                     m_currentAllocatorSlotCopy = 0;
```

- [ ] **Step 3: Rewrite create_compute_command_list() with ring buffer**

In `device_d3d12.cpp`, replace the existing `create_compute_command_list()`:

```cpp
std::unique_ptr<ComputeCommandList> DeviceD3D12::create_compute_command_list() {
    auto cmd = std::make_unique<ComputeCommandListD3D12>(m_device.Get());

    auto& allocator = m_cmdAllocatorsCompute[m_currentAllocatorSlotCompute];

    if (m_cmdAllocatorFenceValuesCompute[m_currentAllocatorSlotCompute] > 0) {
        uint64_t waitValue = m_cmdAllocatorFenceValuesCompute[m_currentAllocatorSlotCompute];
        while (m_fence->GetCompletedValue() < waitValue) {
            HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (event) {
                m_fence->SetEventOnCompletion(waitValue, event);
                WaitForSingleObject(event, INFINITE);
                CloseHandle(event);
            }
        }
    }

    if (!allocator) {
        HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                                       IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            aether::log::error("Failed to create compute command allocator");
            return nullptr;
        }
    }
    allocator->Reset();

    HRESULT hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                              allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&cmd->m_list));
    if (FAILED(hr)) {
        aether::log::error("Failed to create compute command list");
        return nullptr;
    }

    cmd->m_allocator = allocator;
    cmd->m_allocatorSlot = m_currentAllocatorSlotCompute;
    cmd->m_list->Close();

    m_currentAllocatorSlotCompute = (m_currentAllocatorSlotCompute + 1) % kMaxFrameLatency;

    aether::log::debug("Created compute command list (slot {})", cmd->m_allocatorSlot);
    return cmd;
}
```

- [ ] **Step 4: Rewrite get_copy_queue() with ring buffer**

In `device_d3d12.cpp`, replace `get_copy_queue()`:

```cpp
std::unique_ptr<CopyCommandList> DeviceD3D12::get_copy_queue() {
    auto cmd = std::make_unique<CopyCommandListD3D12>(m_device.Get());

    auto& allocator = m_cmdAllocatorsCopy[m_currentAllocatorSlotCopy];

    if (m_cmdAllocatorFenceValuesCopy[m_currentAllocatorSlotCopy] > 0) {
        uint64_t waitValue = m_cmdAllocatorFenceValuesCopy[m_currentAllocatorSlotCopy];
        while (m_fence->GetCompletedValue() < waitValue) {
            HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (event) {
                m_fence->SetEventOnCompletion(waitValue, event);
                WaitForSingleObject(event, INFINITE);
                CloseHandle(event);
            }
        }
    }

    if (!allocator) {
        HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                                       IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            aether::log::error("Failed to create copy command allocator");
            return nullptr;
        }
    }
    allocator->Reset();

    HRESULT hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY,
                                              allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&cmd->m_list));
    if (FAILED(hr)) {
        aether::log::error("Failed to create copy command list");
        return nullptr;
    }

    cmd->m_allocator = allocator;
    cmd->m_allocatorSlot = m_currentAllocatorSlotCopy;
    cmd->m_list->Close();

    m_currentAllocatorSlotCopy = (m_currentAllocatorSlotCopy + 1) % kMaxFrameLatency;

    aether::log::debug("Created copy command list (slot {})", cmd->m_allocatorSlot);
    return cmd;
}
```

- [ ] **Step 5: Extend execute_command_lists() to track compute/copy slots**

In `device_d3d12.cpp` `execute_command_lists()`, add tracking for compute and copy command lists:

```cpp
        } else if (auto* computeCmd = dynamic_cast<ComputeCommandListD3D12*>(cmd.get())) {
            d3dCmdLists.push_back(computeCmd->m_list.Get());
            if (computeCmd->m_allocatorSlot < kMaxFrameLatency) {
                slotsUsed[computeCmd->m_allocatorSlot] = true;
            }
        } else if (auto* copyCmd = dynamic_cast<CopyCommandListD3D12*>(cmd.get())) {
            d3dCmdLists.push_back(copyCmd->m_list.Get());
            if (copyCmd->m_allocatorSlot < kMaxFrameLatency) {
                slotsUsed[copyCmd->m_allocatorSlot] = true;
            }
        }
```

But wait — compute and copy use separate ring buffers (`m_cmdAllocatorsCompute` vs `m_cmdAllocatorsCopy`). The current `slotsUsed` array shares the same fence values as graphics. We need separate tracking.

Add separate fence value tracking in the signal section:

```cpp
        for (UINT i = 0; i < kMaxFrameLatency; ++i) {
            if (slotsUsed[i]) {
                m_cmdAllocatorFenceValues[i] = m_fenceValue;
            }
        }
```

This works because compute/copy allocator slots have their OWN fence value arrays (`m_cmdAllocatorFenceValuesCompute`/`Copy`), and the `slotsUsed` index for compute/copy is their slot index. Let me clarify:

Actually, we need to update BOTH the graphics AND compute/copy fence values. The `slotsUsed` array using a shared array of `kMaxFrameLatency` would incorrectly share indices between graphics and compute allocator slots. We need separate tracking.

Update the signal section to use separate index ranges or check which type each slot belongs to. The simpler approach: use 3 separate `slotsUsed` arrays, one per queue type:

```cpp
    bool gfxSlotsUsed[kMaxFrameLatency] = {};
    bool computeSlotsUsed[kMaxFrameLatency] = {};
    bool copySlotsUsed[kMaxFrameLatency] = {};

    for (auto& cmd : cmds) {
        cmd->close();
        if (auto* gfxCmd = dynamic_cast<GraphicsCommandListD3D12*>(cmd.get())) {
            d3dCmdLists.push_back(gfxCmd->m_list.Get());
            if (gfxCmd->m_allocatorSlot < kMaxFrameLatency)
                gfxSlotsUsed[gfxCmd->m_allocatorSlot] = true;
        } else if (auto* computeCmd = dynamic_cast<ComputeCommandListD3D12*>(cmd.get())) {
            d3dCmdLists.push_back(computeCmd->m_list.Get());
            if (computeCmd->m_allocatorSlot < kMaxFrameLatency)
                computeSlotsUsed[computeCmd->m_allocatorSlot] = true;
        } else if (auto* copyCmd = dynamic_cast<CopyCommandListD3D12*>(cmd.get())) {
            d3dCmdLists.push_back(copyCmd->m_list.Get());
            if (copyCmd->m_allocatorSlot < kMaxFrameLatency)
                copySlotsUsed[copyCmd->m_allocatorSlot] = true;
        }
    }

    if (!d3dCmdLists.empty()) {
        m_directQueue->ExecuteCommandLists(...);
        m_fenceValue++;
        m_directQueue->Signal(m_fence.Get(), m_fenceValue);

        for (UINT i = 0; i < kMaxFrameLatency; ++i) {
            if (gfxSlotsUsed[i])      m_cmdAllocatorFenceValues[i] = m_fenceValue;
            if (computeSlotsUsed[i])   m_cmdAllocatorFenceValuesCompute[i] = m_fenceValue;
            if (copySlotsUsed[i])      m_cmdAllocatorFenceValuesCopy[i] = m_fenceValue;
        }
    }
```

- [ ] **Step 6: Build and verify**

```bash
cd /d D:\Source\AetherAI
cmake --build build --config Debug 2>&1 | tail -20
```
Expected: Clean compile, no errors related to command list changes.

---

### Task 2: FenceD3D12::signal()

**Files:**
- Modify: `src/Aether.RHI/D3D12/resource_d3d12_impl.h`
- Modify: `src/Aether.RHI/D3D12/resource_d3d12.cpp`
- Modify: `src/Aether.RHI/D3D12/device_d3d12.cpp`

- [ ] **Step 1: Add queue pointer to FenceD3D12**

In `resource_d3d12_impl.h`:

```cpp
struct FenceD3D12 : public Fence {
    ComPtr<ID3D12Fence> fence;
    ID3D12CommandQueue* queue;   // non-owning
    HANDLE event;

    FenceD3D12(ID3D12Fence* f, ID3D12CommandQueue* q);
    ~FenceD3D12() override;
    // ...
};
```

- [ ] **Step 2: Update constructor**

In `resource_d3d12.cpp`:

```cpp
FenceD3D12::FenceD3D12(ID3D12Fence* f, ID3D12CommandQueue* q)
    : fence(f), queue(q) {
    event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void FenceD3D12::signal(uint64_t value) {
    queue->Signal(fence.Get(), value);
}
```

- [ ] **Step 3: Update create_fence() in device_d3d12.cpp**

Find `DeviceD3D12::create_fence()`:

```cpp
FencePtr DeviceD3D12::create_fence(uint64_t initialValue) {
    ComPtr<ID3D12Fence> d3dFence;
    HRESULT hr = m_device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&d3dFence));
    if (FAILED(hr)) {
        aether::log::error("Failed to create fence");
        return nullptr;
    }
    return std::make_shared<FenceD3D12>(d3dFence.Detach(), m_directQueue.Get());
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --config Debug 2>&1 | tail -10
```
Expected: Clean compile.

---

### Task 3: Descriptor Heap + ShaderBinding

**Files:**
- Modify: `src/Aether.RHI/src/aether.rhi.cppm` — BindingLayout add numDescriptors
- Modify: `src/Aether.RHI/D3D12/device_d3d12_impl.h` — DescriptorAllocator, ShaderBindingD3D12, root signature fields
- Modify: `src/Aether.RHI/D3D12/device_d3d12.cpp` — create_default_root_signature, descriptor heap init, create_shader_binding
- Modify: `src/Aether.RHI/D3D12/command_list_d3d12_impl.h` — ShaderBindingD3D12 forward decl
- Modify: `src/Aether.RHI/D3D12/command_list_d3d12.cpp` — bind_descriptor() for graphics + compute
- Modify: `src/Aether.RHI/D3D12/pipeline_d3d12.cpp` — (maybe) root sig changes

- [ ] **Step 1: Update BindingLayout in aether.rhi.cppm**

```cpp
struct BindingLayout {
    uint32_t numDescriptors = 0;
};
```

- [ ] **Step 2: Add DescriptorAllocator + ShaderBindingD3D12 to device_d3d12_impl.h**

After `#include "resource_d3d12_impl.h"`:

```cpp
#include "pipeline_d3d12_impl.h"
```

Add before `class DeviceD3D12`:

```cpp
struct DescriptorAllocator {
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
    UINT descriptorSize = 0;
    UINT capacity = 10000;
    UINT currentOffset = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE alloc_cpu(UINT count = 1);
    D3D12_GPU_DESCRIPTOR_HANDLE alloc_gpu(UINT count = 1);
    void reset();
};

struct ShaderBindingD3D12 : public ShaderBinding {
    ID3D12Device10* m_device;
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle = {};
    UINT m_descriptorCount = 0;
    UINT m_descriptorSize = 0;

    ShaderBindingD3D12(ID3D12Device10* device,
                       D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
                       D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
                       UINT count, UINT descriptorSize);

    void set_buffer(uint32_t slot, BufferPtr buffer, uint64_t offset) override;
    void set_texture(uint32_t slot, TexturePtr texture) override;
};
```

In `DeviceD3D12` class, add member:

```cpp
    DescriptorAllocator m_descriptorAllocator;
```

- [ ] **Step 3: Implement DescriptorAllocator methods and ShaderBindingD3D12 in device_d3d12.cpp**

Add at the top of the `aether::rhi` namespace, after the `#include` section:

```cpp
namespace aether::rhi {

// === DescriptorAllocator ===
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::alloc_cpu(UINT count) {
    if (currentOffset + count > capacity) {
        aether::log::error("DescriptorAllocator: out of descriptors ({}/{})",
                           currentOffset + count, capacity);
        return {0};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE handle = {cpuStart.ptr + currentOffset * descriptorSize};
    currentOffset += count;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::alloc_gpu(UINT count) {
    if (currentOffset + count > capacity) {
        aether::log::error("DescriptorAllocator: out of descriptors ({}/{})",
                           currentOffset + count, capacity);
        return {0};
    }
    D3D12_GPU_DESCRIPTOR_HANDLE handle = {gpuStart.ptr + currentOffset * descriptorSize};
    currentOffset += count;
    return handle;
}

void DescriptorAllocator::reset() {
    currentOffset = 0;
}

// === ShaderBindingD3D12 ===
ShaderBindingD3D12::ShaderBindingD3D12(ID3D12Device10* device,
                                       D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
                                       D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
                                       UINT count, UINT descriptorSize)
    : m_device(device), m_gpuHandle(gpuStart), m_cpuHandle(cpuStart),
      m_descriptorCount(count), m_descriptorSize(descriptorSize) {}

void ShaderBindingD3D12::set_buffer(uint32_t slot, BufferPtr buffer, uint64_t offset) {
    if (slot >= m_descriptorCount) return;
    auto* buf = static_cast<BufferD3D12*>(buffer.get());
    if (!buf || !buf->resource) return;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {
        m_cpuHandle.ptr + slot * m_descriptorSize
    };

    // slot 0-13: CBV, slot 14+: SRV
    if (slot < 14) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation = buf->resource->GetGPUVirtualAddress() + offset;
        cbvDesc.SizeInBytes = AlignTo(buf->desc.size, 256);
        m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);
    } else {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = offset / sizeof(uint32_t);
        srvDesc.Buffer.NumElements = static_cast<UINT>(buf->desc.size / sizeof(uint32_t));
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_device->CreateShaderResourceView(buf->resource.Get(), &srvDesc, cpuHandle);
    }
}

void ShaderBindingD3D12::set_texture(uint32_t slot, TexturePtr texture) {
    if (slot >= m_descriptorCount) return;
    auto* tex = static_cast<TextureD3D12*>(texture.get());
    if (!tex || !tex->resource) return;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {
        m_cpuHandle.ptr + slot * m_descriptorSize
    };

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = static_cast<DXGI_FORMAT>(tex->resource->GetDesc().Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = tex->desc.mipLevels;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    m_device->CreateShaderResourceView(tex->resource.Get(), &srvDesc, cpuHandle);
}
```

Also add a helper for size alignment (needed by CBV):
```cpp
// In an appropriate namespace section
namespace {
    UINT AlignTo(UINT size, UINT alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }
}
// OR use the existing pattern with std::bit_ceil etc.
// Simpler: just add a local helper before set_buffer
```

- [ ] **Step 4: Update create_default_root_signature() with descriptor table + sampler**

In `device_d3d12.cpp`, replace `create_default_root_signature()`:

```cpp
void DeviceD3D12::create_default_root_signature() {
    // RootParam[0]: Descriptor table (CBV b0-b13, SRV t0-t127, UAV u0-u63)
    D3D12_DESCRIPTOR_RANGE1 ranges[1] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 14;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Actually we need separate ranges for CBV and SRV
    // Let's use CD3DX12 for simplicity
}
```

Actually, for a descriptor table with multiple ranges (CBV + SRV + UAV), the D3D12 API requires either separate ranges in one table or separate tables. One table with multiple ranges is cleaner:

```cpp
void DeviceD3D12::create_default_root_signature() {
    D3D12_DESCRIPTOR_RANGE1 ranges[3] = {};
    
    // CBV b0-b13
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 14;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // SRV t0-t127
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 128;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // UAV u0-u63
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 64;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].RegisterSpace = 0;
    ranges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 params[2] = {};
    
    // Param[0]: Descriptor Table
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 3;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param[1]: Root CBV (b14, reserved for per-draw constants)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].Descriptor.ShaderRegister = 14;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static Sampler (s0, linear clamp)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc{};
    sigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    sigDesc.Desc_1_1.NumParameters = 2;
    sigDesc.Desc_1_1.pParameters = params;
    sigDesc.Desc_1_1.NumStaticSamplers = 1;
    sigDesc.Desc_1_1.pStaticSamplers = &sampler;
    sigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&sigDesc, &serialized, &error);
    if (FAILED(hr)) {
        aether::log::error("Failed to serialize root signature: {}",
                           error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown");
        return;
    }

    hr = m_device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                        serialized->GetBufferSize(),
                                        IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(hr)) {
        aether::log::error("Failed to create root signature");
        return;
    }
    aether::log::debug("Default root signature created (descriptor table + root CBV + sampler)");
}
```

- [ ] **Step 5: Initialize descriptor heap in DeviceD3D12::initialize()**

In `device_d3d12.cpp`, after `create_default_root_signature()` call, add:

```cpp
    // Create global descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = m_descriptorAllocator.capacity;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descHeap;
    if (SUCCEEDED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descHeap)))) {
        m_descriptorAllocator.heap = descHeap;
        m_descriptorAllocator.cpuStart = descHeap->GetCPUDescriptorHandleForHeapStart();
        m_descriptorAllocator.gpuStart = descHeap->GetGPUDescriptorHandleForHeapStart();
        m_descriptorAllocator.descriptorSize = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        aether::log::debug("Created global descriptor heap ({} descriptors)", m_descriptorAllocator.capacity);
    } else {
        aether::log::error("Failed to create global descriptor heap");
    }
```

- [ ] **Step 6: Implement create_shader_binding()**

In `device_d3d12.cpp`:

```cpp
ShaderBindingPtr DeviceD3D12::create_shader_binding(const BindingLayout& layout) {
    if (layout.numDescriptors == 0) {
        aether::log::warn("create_shader_binding: numDescriptors is 0");
        return nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorAllocator.alloc_cpu(layout.numDescriptors);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorAllocator.alloc_gpu(layout.numDescriptors);

    if (cpuHandle.ptr == 0 || gpuHandle.ptr == 0) {
        aether::log::error("create_shader_binding: descriptor allocation failed");
        return nullptr;
    }

    return std::make_shared<ShaderBindingD3D12>(
        m_device.Get(), cpuHandle, gpuHandle,
        layout.numDescriptors, m_descriptorAllocator.descriptorSize);
}
```

Also add the method declaration in `device_d3d12_impl.h` if not already declared (it should be, since it's a virtual override).

- [ ] **Step 7: Reset descriptor allocator in create_graphics_command_list()**

In `device_d3d12.cpp` `create_graphics_command_list()`, after the fence wait block, add:

```cpp
    // GPU is done with previous work — safe to recycle descriptors
    m_descriptorAllocator.reset();
```

- [ ] **Step 8: Implement bind_descriptor() for Graphics and Compute**

In `command_list_d3d12.cpp`:

```cpp
void GraphicsCommandListD3D12::bind_descriptor(uint32_t /*slot*/, ShaderBinding* binding) {
    auto* sb = dynamic_cast<ShaderBindingD3D12*>(binding);
    if (sb) {
        ID3D12DescriptorHeap* heaps[] = { sb->m_device->GetCPUDescriptorHandleForHeapStart() };
        // Actually, we need to get the descriptor heap from the binding
    }
}
```

Hmm wait, this is more complex. The descriptor heap needs to be set via `SetDescriptorHeaps()` on the command list before `SetGraphicsRootDescriptorTable()` can work. The heap is the global one stored in the device.

We need a way for the command list to access the device's descriptor heap. Options:
1. Store a pointer to the Device in the command list (already has `m_device`)
2. Store the descriptor heap pointer + handle in the command list

Option 2 is cleaner. Add to `GraphicsCommandListD3D12` and `ComputeCommandListD3D12`:
```cpp
ID3D12DescriptorHeap* m_descriptorHeap = nullptr;
```

And set it during creation. But actually, the command list already stores `m_device` (ID3D12Device10*). We could store the heap pointer separately.

Simpler: have the device set the descriptor heap on the command list during creation. But actually, the descriptor heap needs to be set via `SetDescriptorHeaps` on the command list itself, which means after `CreateCommandList`. The device knows the heap.

Add `ID3D12DescriptorHeap* m_descriptorHeap` to GraphicsCommandListD3D12 and set it in `create_graphics_command_list()`.

Actually, let me just store the descriptor heap pointer directly.

In `command_list_d3d12_impl.h`:
```cpp
struct GraphicsCommandListD3D12 : public GraphicsCommandList {
    // ... existing fields ...
    ID3D12DescriptorHeap* m_descriptorHeap = nullptr;
    // ...
};
```

In `device_d3d12.cpp` `create_graphics_command_list()`, after `CreateCommandList`:
```cpp
    cmd->m_descriptorHeap = m_descriptorAllocator.heap.Get();
```

Then `bind_descriptor()`:
```cpp
void GraphicsCommandListD3D12::bind_descriptor(uint32_t /*slot*/, ShaderBinding* binding) {
    auto* sb = dynamic_cast<ShaderBindingD3D12*>(binding);
    if (sb && m_descriptorHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
        m_list->SetDescriptorHeaps(1, heaps);
        m_list->SetGraphicsRootDescriptorTable(0, sb->m_gpuHandle);
    }
}
```

Wait, `SetDescriptorHeaps` should only be called once if the heap doesn't change. Calling it redundantly is valid but wasteful. For the triangle example it's fine. We can optimize later.

Same for compute:

```cpp
void ComputeCommandListD3D12::bind_descriptor(uint32_t /*slot*/, ShaderBinding* binding) {
    auto* sb = dynamic_cast<ShaderBindingD3D12*>(binding);
    if (sb && m_descriptorHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorHeap };
        m_list->SetDescriptorHeaps(1, heaps);
        m_list->SetComputeRootDescriptorTable(0, sb->m_gpuHandle);
    }
}
```

Add `m_descriptorHeap` to `ComputeCommandListD3D12` and set it in `create_compute_command_list()`.

- [ ] **Step 9: Build and verify**

```bash
cmake --build build --config Debug 2>&1 | tail -30
```
Expected: Clean compile.

- [ ] **Step 10: Add #include <d3d12shader.h> to d3d12_common.h if not present**

Already added in earlier changes. Verify it's there.

- [ ] **Step 11: Run the triangle example to verify no regressions**

```bash
./build/examples/triangle/Debug/TriangleExample.exe
```
Expected: Triangle renders, no crash, clean exit.

---

### Task 4: Build and verify everything together

- [ ] **Step 1: Full build**

```bash
cd /d D:\Source\AetherAI
cmake --build build --config Debug 2>&1
```
Expected: All projects compile with zero errors.

- [ ] **Step 2: Verify with debug layer output**

Run the triangle example with `--debug` or via VS to capture debug layer output.
Expected: No D3D12 errors about allocator lifetime, descriptor heap, or fence operations.

- [ ] **Step 3: Commit all changes**

```bash
git add src/ docs/
git commit -m "fix: P0 RHI consolidation - allocator ring buffers, descriptor heap, fence signal"
```

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
