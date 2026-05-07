#pragma once
#include "d3d12_common.h"
#include "resource_d3d12_impl.h"

namespace aether::rhi {

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

class DeviceD3D12 : public Device {
public:
    static constexpr UINT kMaxFrameLatency = 3;

    DeviceD3D12();
    ~DeviceD3D12() override;

    // Resource creation
    BufferPtr              create_buffer(const BufferDesc& desc) override;
    TexturePtr             create_texture(const TextureDesc& desc) override;
    GraphicsPipelinePtr    create_graphics_pipeline(const GfxPipelineDesc& desc) override;
    ComputePipelinePtr     create_compute_pipeline(const ComputePipelineDesc& desc) override;
    RayTracingPipelinePtr  create_ray_tracing_pipeline(const RTPipelineDesc& desc) override;
    ShaderBindingPtr       create_shader_binding(const BindingLayout& layout) override;
    FencePtr               create_fence(uint64_t initialValue = 0) override;
    SwapChainPtr           create_swap_chain(const SwapChainDesc& desc) override;

    std::unique_ptr<GraphicsCommandList> create_graphics_command_list() override;
    std::unique_ptr<ComputeCommandList>  create_compute_command_list() override;
    std::unique_ptr<CopyCommandList>     get_copy_queue() override;

    void execute_command_lists(std::span<std::unique_ptr<CommandList>> cmds) override;
    void wait_for_idle() override;

private:
    void initialize();
    void create_factory();
    void create_adapter();
    void create_direct_queue();
    void create_copy_queue();
    void create_default_root_signature();

    ComPtr<IDXGIFactory6>    m_factory;
    ComPtr<ID3D12Device10>   m_device;
    ComPtr<ID3D12CommandQueue> m_directQueue;
    ComPtr<ID3D12CommandQueue> m_copyQueue;
    ComPtr<ID3D12Fence>      m_fence;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    uint64_t                 m_fenceValue = 0;

    // Ring buffer for graphics command allocator recycling
    ComPtr<ID3D12CommandAllocator> m_cmdAllocators[kMaxFrameLatency];
    uint64_t                 m_cmdAllocatorFenceValues[kMaxFrameLatency] = {};
    UINT                     m_currentAllocatorSlot = 0;

    // Ring buffer for compute command allocator recycling
    ComPtr<ID3D12CommandAllocator> m_cmdAllocatorsCompute[kMaxFrameLatency];
    uint64_t                 m_cmdAllocatorFenceValuesCompute[kMaxFrameLatency] = {};
    UINT                     m_currentAllocatorSlotCompute = 0;

    // Ring buffer for copy command allocator recycling
    ComPtr<ID3D12CommandAllocator> m_cmdAllocatorsCopy[kMaxFrameLatency];
    uint64_t                 m_cmdAllocatorFenceValuesCopy[kMaxFrameLatency] = {};
    UINT                     m_currentAllocatorSlotCopy = 0;

    DescriptorAllocator      m_descriptorAllocator;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    UINT                     m_dsvDescriptorSize = 0;
    UINT                     m_dsvHeapOffset = 0;

    ComPtr<ID3D12CommandSignature> m_commandSignature;
};

// ResourceState → D3D12_RESOURCE_STATES conversion
inline D3D12_RESOURCE_STATES to_d3d12_resource_state(ResourceState state) {
    switch (state) {
        case ResourceState::Common:                return D3D12_RESOURCE_STATE_COMMON;
        case ResourceState::RenderTarget:          return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case ResourceState::CopySource:            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case ResourceState::CopyDest:              return D3D12_RESOURCE_STATE_COPY_DEST;
        case ResourceState::VertexAndConstantBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case ResourceState::IndexBuffer:           return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case ResourceState::UnorderedAccess:       return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case ResourceState::DepthStencilWrite:     return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case ResourceState::DepthStencilRead:      return D3D12_RESOURCE_STATE_DEPTH_READ;
        case ResourceState::ShaderResource:        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case ResourceState::IndirectArgument:      return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        case ResourceState::GenericRead:           return D3D12_RESOURCE_STATE_GENERIC_READ;
        case ResourceState::StreamOut:             return D3D12_RESOURCE_STATE_STREAM_OUT;
        case ResourceState::Predication:           return D3D12_RESOURCE_STATE_PREDICATION;
        default:                                   return D3D12_RESOURCE_STATE_COMMON;
    }
}

} // namespace aether::rhi
