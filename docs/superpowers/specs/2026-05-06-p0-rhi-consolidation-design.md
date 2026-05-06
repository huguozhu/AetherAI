# P0: RHI Core Consolidation

Date: 2026-05-06
Goal: Stabilize D3D12 RHI backend and enable shader resource binding

## Overview

P0 consolidates the D3D12 RHI backend by fixing three remaining gaps:
1. Compute/Copy command allocator lifecycle (same fix as graphics)
2. Descriptor heap + ShaderBinding system (enables shader resource binding)
3. FenceD3D12::signal() implementation

These are strict prerequisites for any further development (textures, constant buffers, compute-based culling).

---

## Item 1: Compute/Copy Allocator Ring Buffer

**Problem:** `create_compute_command_list()` and `get_copy_queue()` create a new `ID3D12CommandAllocator` per call. The allocator is released when the wrapper destructs, but the GPU may still be executing. Random use-after-free crash.

**Fix:** Same ring buffer pattern already applied to graphics command lists.

### Changes

**command_list_d3d12_impl.h**
- `ComputeCommandListD3D12`: add `UINT m_allocatorSlot = UINT_MAX`
- `CopyCommandListD3D12`: add `UINT m_allocatorSlot = UINT_MAX`

**device_d3d12_impl.h**
- `ComPtr<ID3D12CommandAllocator> m_cmdAllocatorsCompute[kMaxFrameLatency]`
- `ComPtr<ID3D12CommandAllocator> m_cmdAllocatorsCopy[kMaxFrameLatency]`
- `uint64_t m_cmdAllocatorFenceValuesCompute[kMaxFrameLatency] = {}`
- `uint64_t m_cmdAllocatorFenceValuesCopy[kMaxFrameLatency] = {}`
- `UINT m_currentAllocatorSlotCompute = 0`
- `UINT m_currentAllocatorSlotCopy = 0`

**device_d3d12.cpp — create_compute_command_list()**
1. Select slot at `m_currentAllocatorSlotCompute`
2. Wait for fence at `m_cmdAllocatorFenceValuesCompute[slot]` if > 0
3. Create allocator if first use, else `Reset()`
4. `CreateCommandList` with type `D3D12_COMMAND_LIST_TYPE_COMPUTE`
5. Tag `cmd->m_allocatorSlot`, advance slot

**device_d3d12.cpp — get_copy_queue()**
Same pattern, using `D3D12_COMMAND_LIST_TYPE_COPY`.

**device_d3d12.cpp — execute_command_lists()**
Extend `slotsUsed[]` tracking to `ComputeCommandListD3D12` and `CopyCommandListD3D12`, using a single flat array of `kMaxFrameLatency * 3` or separate tracking.

---

## Item 2: Descriptor Heap + ShaderBinding

### 2a. Root Signature

Replace the current empty root signature with a minimal one:

```
RootParam[0]: Descriptor Table, visibility=D3D12_SHADER_VISIBILITY_ALL
  CBV b0-b13  (14 slots)
  SRV t0-t127 (128 slots)
  UAV u0-u63  (64 slots)

RootParam[1]: Root CBV b14, visibility=D3D12_SHADER_VISIBILITY_ALL
  (for per-draw MVP/constants; reserved, not wired in first pass)

StaticSampler[0]: default linear clamp sampler (register s0)
```

### 2b. Global Descriptor Heap

Created once at device init:

- Type: `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV`
- Size: 10000 descriptors
- Flags: `D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE`

### 2c. Descriptor Bump Allocator

```cpp
struct DescriptorAllocator {
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart;
    UINT descriptorSize;    // GetDescriptorHandleIncrementSize
    UINT capacity;          // 10000
    UINT currentOffset;     // bump counter, reset per frame

    D3D12_CPU_DESCRIPTOR_HANDLE alloc_cpu(UINT count = 1);
    D3D12_GPU_DESCRIPTOR_HANDLE alloc_gpu(UINT count = 1);
    void reset();
};
```

- `alloc_*` returns current handle then advances by `count`
- On overflow: logs error, returns null handle (zero.ptr)
- `reset()` sets `currentOffset = 0`
- Stored in `DeviceD3D12`, accessible during ShaderBinding creation and binding dispatch

### 2d. BindingLayout

Extend the currently empty `BindingLayout` struct to carry a descriptor count:

```cpp
struct BindingLayout {
    uint32_t numDescriptors = 0;   // number of contiguous descriptor slots to allocate
};
```

Each binding pre-allocates `numDescriptors` contiguous slots from the bump allocator.
`set_buffer(slot, ...)` and `set_texture(slot, ...)` write to the pre-allocated slot
at offset `(baseCpuHandle + slot * descriptorSize)`.
No new allocations happen during `set_*` calls — all memory is reserved at binding creation.
`slot` must be < `numDescriptors`.

### 2e. ShaderBindingD3D12

```cpp
ShaderBindingD3D12 : ShaderBinding {
    D3D12_GPU_DESCRIPTOR_HANDLE m_baseGpuHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE m_baseCpuHandle;
    UINT m_descriptorCount;
    ID3D12Device10* m_device;

    ShaderBindingD3D12(
        ID3D12Device10* device,
        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
        UINT descriptorCount);

    void set_buffer(uint32_t slot, BufferPtr buffer, uint64_t offset) override;
    void set_texture(uint32_t slot, TexturePtr texture) override;
    D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_handle() const;
};
```

**set_buffer logic:**
- `slot` maps to register space (0-13 = CBV b0-b13, 14+ = SRV or reserved)
- Allocates CPU handle at `m_baseCpuHandle + slot * descriptorSize`
- For CBV: creates `D3D12_CONSTANT_BUFFER_VIEW_DESC` with buffer's GPU VA, size aligned to 256
- For SRV: creates `D3D12_SHADER_RESOURCE_VIEW_DESC` with buffer's GPU VA, format=R32_FLOAT / structed buffer desc

**set_texture logic:**
- Slot maps to SRV register t0-t127
- Creates `D3D12_SHADER_RESOURCE_VIEW_DESC` from texture format/dimension

### 2f. bind_descriptor

On `GraphicsCommandListD3D12::bind_descriptor()`:

```cpp
m_list->SetGraphicsRootDescriptorTable(rootParamIndex, binding->get_gpu_handle());
```

Root parameter index for the descriptor table is fixed at 0.

Similarly, `ComputeCommandListD3D12::bind_descriptor()` does the same via `SetComputeRootDescriptorTable()`.

### 2g. Device creation flow

In `create_default_root_signature()`:
1. Define 2 root parameters (descriptor table + root CBV)
2. Define 1 static sampler
3. Serialize + create

In `DeviceD3D12::initialize()`:
1. After device creation → `create_default_root_signature()`
2. After root signature → create descriptor heap (10000 entries, shader-visible)

### 2h. Frame fence slot tracking in descriptor allocator

The descriptor allocator bumps per `create_shader_binding()` call. Per-frame reset must happen after GPU is done with that frame's descriptors.

Reset strategy: called in `create_graphics_command_list()` after the fence wait.
- The fence wait (e.g. waiting for slot 0's fence V1) guarantees ALL prior work on the queue is done (fence values are monotonically increasing on a single queue).
- Therefore ALL previously allocated descriptor regions are safe to recycle.
- After reset, the bump starts from offset 0 again.

This ties naturally into the existing ring buffer lifecycle with zero additional synchronization overhead.

---

## Item 3: FenceD3D12::signal()

**Current:** Empty method body.

**Fix:**

```cpp
struct FenceD3D12 : public Fence {
    ComPtr<ID3D12Fence> fence;
    ID3D12CommandQueue* queue;   // non-owning pointer
    HANDLE event;

    FenceD3D12(ID3D12Fence* f, ID3D12CommandQueue* q)
        : fence(f), queue(q) { event = CreateEvent(...); }

    void signal(uint64_t value) override {
        queue->Signal(fence.Get(), value);
    }
};
```

`DeviceD3D12::create_fence()` passes `m_directQueue.Get()` as the queue pointer.

---

## File Change Summary

| File | Change Type |
|------|-------------|
| `device_d3d12_impl.h` | Add ring buffer fields + descriptor heap fields |
| `command_list_d3d12_impl.h` | Add `m_allocatorSlot` to Compute/Copy |
| `device_d3d12.cpp` | Ring buffer logic, fence signal in create_fence |
| `pipeline_d3d12.cpp` | No changes (root sig already created) |
| `command_list_d3d12.cpp` | `bind_descriptor()` implementation |
| `resource_d3d12.cpp` | No changes needed |
| `main.cpp` | No changes (but can now use ShaderBinding) |

## Success Criteria

- Triangle example runs without crashes (existing allocator fix verified)
- `create_shader_binding()` returns non-null binding
- `set_buffer()` creates valid CBV/SRV descriptors visible in debug layer
- `FenceD3D12::signal()` correctly signals the GPU fence (verifiable via debug layer or trace)
