module;
#include "d3d12_common.h"
module aether.rhi;

#include "device_d3d12_impl.h"
#include "command_list_d3d12_impl.h"

import aether.core;
import <utility>;
import <memory>;
import <span>;
import <string>;
import <vector>;
import <stdexcept>;

namespace aether::rhi {

namespace {
    UINT align_to(UINT size, UINT alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    }
}

// Factory function
std::unique_ptr<Device> create_d3d12_device() {
    return std::make_unique<DeviceD3D12>();
}

DeviceD3D12::DeviceD3D12() {
    aether::log::info("Creating D3D12 device...");
    initialize();
}

DeviceD3D12::~DeviceD3D12() {
    wait_for_idle();
}

void DeviceD3D12::initialize() {
    // Enable D3D12 debug layer
    ComPtr<ID3D12Debug> debugCtrl;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugCtrl)))) {
        debugCtrl->EnableDebugLayer();
        aether::log::info("D3D12 debug layer enabled");
    }

    create_factory();
    create_adapter();
    create_direct_queue();
    create_copy_queue();
    create_default_root_signature();

    // Create DSV heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 100;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (SUCCEEDED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)))) {
            m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            aether::log::debug("Created DSV heap ({} descriptors)", 100);
        } else {
            aether::log::error("Failed to create DSV heap");
        }
    }

    // Create global descriptor heap for CBV/SRV/UAV
    {
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
    }

    // Create command signature for indirect draw
    {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc{};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC cmdSigDesc{};
        cmdSigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        cmdSigDesc.NumArgumentDescs = 1;
        cmdSigDesc.pArgumentDescs = &argDesc;

        if (FAILED(m_device->CreateCommandSignature(&cmdSigDesc, nullptr,
                                                     IID_PPV_ARGS(&m_commandSignature)))) {
            aether::log::error("Failed to create command signature for indirect draw");
        } else {
            aether::log::debug("Command signature created for indirect draw");
        }
    }

    // Create a fence for CPU-GPU synchronization
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) {
        aether::log::error("Failed to create D3D12 fence");
        return;
    }
    m_fenceValue = 1;

    aether::log::info("D3D12 device initialized successfully");
}

void DeviceD3D12::create_factory() {
    UINT flags = 0;
#ifdef _DEBUG
    // Enable debug layer in debug builds
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        flags = DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        aether::log::error("Failed to create DXGI factory");
    }
}

void DeviceD3D12::create_adapter() {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        // Try to create a D3D12 device on this adapter
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                        IID_PPV_ARGS(&m_device)))) {
            std::wstring wideDesc(desc.Description);
            aether::log::info("Using adapter: {}",
                              std::string(wideDesc.begin(), wideDesc.end()));
            break;
        }
    }
    if (!m_device) {
        aether::log::error("No suitable D3D12 adapter found!");
    }
}

void DeviceD3D12::create_direct_queue() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    HRESULT hr = m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_directQueue));
    if (FAILED(hr)) {
        aether::log::error("Failed to create direct command queue");
    }
}

void DeviceD3D12::create_copy_queue() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    HRESULT hr = m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_copyQueue));
    if (FAILED(hr)) {
        aether::log::error("Failed to create copy command queue");
    }
}

void DeviceD3D12::create_default_root_signature() {
    // Descriptor ranges for the descriptor table (Param[0])
    D3D12_DESCRIPTOR_RANGE ranges[3] = {};

    // Range[0]: CBV b0-b13 (14 slots)
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[0].NumDescriptors = 14;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Range[1]: SRV t0-t127 (128 slots)
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 128;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Range[2]: UAV u0-u63 (64 slots)
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[2].NumDescriptors = 64;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].RegisterSpace = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Root parameters
    D3D12_ROOT_PARAMETER params[2] = {};

    // Param[0]: Descriptor Table (all visibility, covers CBV+SRV+UAV)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 3;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Param[1]: Root CBV b14 (reserved for per-draw constants)
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 14;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Static sampler: linear clamp, register s0
    D3D12_STATIC_SAMPLER_DESC sampler{};
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
    sigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
    sigDesc.Desc_1_0.NumParameters = 2;
    sigDesc.Desc_1_0.pParameters = params;
    sigDesc.Desc_1_0.NumStaticSamplers = 1;
    sigDesc.Desc_1_0.pStaticSamplers = &sampler;
    sigDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

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

BufferPtr DeviceD3D12::create_buffer(const BufferDesc& desc) {
    auto buffer = std::make_shared<BufferD3D12>();
    buffer->desc = desc;

    D3D12_HEAP_TYPE heapType;
    D3D12_RESOURCE_STATES initialState;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    switch (desc.heap) {
        case HeapType::Upload:
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case HeapType::Readback:
            heapType = D3D12_HEAP_TYPE_READBACK;
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
        default: // Default
            heapType = D3D12_HEAP_TYPE_DEFAULT;
            initialState = D3D12_RESOURCE_STATE_COMMON;
            break;
    }

    if (static_cast<uint8_t>(desc.bindFlags) & static_cast<uint8_t>(BindFlags::UnorderedAccess)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = heapType;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    // D3D12 upload heap buffers must meet minimum size requirements.
    // Small buffers (< 4KB) can fail on some adapters/drivers.
    if (heapType == D3D12_HEAP_TYPE_UPLOAD && desc.size < 4096) {
        resourceDesc.Width = 4096;
        aether::log::warn("create_buffer: padded upload heap buffer from {} to 4096 bytes", desc.size);
    } else {
        resourceDesc.Width = desc.size;
    }
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = flags;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&buffer->resource));

    if (FAILED(hr)) {
        aether::log::error("Failed to create buffer of size {} bytes (Width={}, hr={:#010x}, heap={}, flags={})",
                           desc.size, static_cast<uint64_t>(resourceDesc.Width),
                           static_cast<unsigned>(hr),
                           static_cast<int>(desc.heap),
                           static_cast<int>(desc.bindFlags));
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT reason = m_device->GetDeviceRemovedReason();
            aether::log::error("D3D12 device removed! GetDeviceRemovedReason() = {:#010x}",
                               static_cast<unsigned>(reason));
        }
        return nullptr;
    }

    buffer->initialState = initialState;
    aether::log::debug("Created buffer: {} bytes, heap={}", desc.size, static_cast<int>(desc.heap));
    return buffer;
}

TexturePtr DeviceD3D12::create_texture(const TextureDesc& desc) {
    auto texture = std::make_shared<TextureD3D12>();
    texture->desc = desc;

    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    switch (desc.format) {
        case Format::R8G8B8A8_UNORM:       dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case Format::R16G16B16A16_FLOAT:   dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case Format::R32G32B32A32_FLOAT:   dxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT; break;
        case Format::R32_FLOAT:            dxgiFormat = DXGI_FORMAT_R32_FLOAT; break;
        case Format::D32_FLOAT:            dxgiFormat = DXGI_FORMAT_D32_FLOAT; break;
        case Format::D24_UNORM_S8_UINT:    dxgiFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; break;
        default: break;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    bool isDepthFormat = (desc.format == Format::D32_FLOAT || desc.format == Format::D24_UNORM_S8_UINT);

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = desc.depth;
    resourceDesc.MipLevels = desc.mipLevels;
    resourceDesc.Format = dxgiFormat;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (isDepthFormat) {
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    D3D12_RESOURCE_STATES initialState = isDepthFormat
        ? D3D12_RESOURCE_STATE_DEPTH_WRITE
        : D3D12_RESOURCE_STATE_COMMON;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&texture->resource));

    if (FAILED(hr)) {
        aether::log::error("Failed to create texture {}x{} fmt={}", desc.width, desc.height, static_cast<int>(desc.format));
        return nullptr;
    }

    texture->initialState = initialState;

    // Create DSV for depth textures
    if (isDepthFormat && m_dsvHeap) {
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsvHandle.ptr += m_dsvHeapOffset * m_dsvDescriptorSize;
        m_device->CreateDepthStencilView(texture->resource.Get(), nullptr, dsvHandle);
        texture->dsvHandle = dsvHandle;
        texture->hasDsvHandle = true;
        m_dsvHeapOffset++;
    }
    aether::log::debug("Created texture: {}x{} format={}", desc.width, desc.height, static_cast<int>(desc.format));
    return texture;
}

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

ShaderBindingPtr DeviceD3D12::create_shader_binding(const BindingLayout& layout) {
    if (layout.numDescriptors == 0) {
        aether::log::warn("create_shader_binding: numDescriptors is 0");
        return nullptr;
    }
    auto handles = m_descriptorAllocator.alloc(layout.numDescriptors);
    if (handles.cpu.ptr == 0 || handles.gpu.ptr == 0) {
        aether::log::error("create_shader_binding: descriptor allocation failed");
        return nullptr;
    }
    return std::make_shared<ShaderBindingD3D12>(
        m_device.Get(), handles.cpu, handles.gpu,
        layout.numDescriptors, m_descriptorAllocator.descriptorSize);
}

std::unique_ptr<GraphicsCommandList> DeviceD3D12::create_graphics_command_list() {
    auto cmd = std::make_unique<GraphicsCommandListD3D12>(m_device.Get());

    auto& allocator = m_cmdAllocators[m_currentAllocatorSlot];

    // Wait for GPU to finish with this slot's allocator before recycling
    if (m_cmdAllocatorFenceValues[m_currentAllocatorSlot] > 0) {
        uint64_t waitValue = m_cmdAllocatorFenceValues[m_currentAllocatorSlot];
        while (m_fence->GetCompletedValue() < waitValue) {
            HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (event) {
                m_fence->SetEventOnCompletion(waitValue, event);
                WaitForSingleObject(event, INFINITE);
                CloseHandle(event);
            }
        }
    }

    // GPU is done with all prior work — safe to recycle descriptors
    m_descriptorAllocator.reset();

    // Create allocator on first use
    if (!allocator) {
        HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&allocator));
        if (FAILED(hr)) {
            aether::log::error("Failed to create graphics command allocator");
            return nullptr;
        }
    }
    allocator->Reset();

    // Create command list
    HRESULT hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&cmd->m_list));
    if (FAILED(hr)) {
        aether::log::error("Failed to create graphics command list");
        return nullptr;
    }

    // Tag the command list with its allocator slot for fence tracking
    cmd->m_allocator = allocator;
    cmd->m_allocatorSlot = m_currentAllocatorSlot;
    cmd->m_descriptorHeap = m_descriptorAllocator.heap.Get();
    cmd->m_rootSignature = m_rootSignature.Get();
    cmd->m_commandSignature = m_commandSignature.Get();
    cmd->m_list->Close();

    // Advance slot for next allocation
    m_currentAllocatorSlot = (m_currentAllocatorSlot + 1) % kMaxFrameLatency;

    aether::log::debug("Created graphics command list (slot {})", cmd->m_allocatorSlot);
    return cmd;
}

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
    cmd->m_descriptorHeap = m_descriptorAllocator.heap.Get();
    cmd->m_list->Close();

    m_currentAllocatorSlotCompute = (m_currentAllocatorSlotCompute + 1) % kMaxFrameLatency;

    aether::log::debug("Created compute command list (slot {})", cmd->m_allocatorSlot);
    return cmd;
}

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

void DeviceD3D12::execute_command_lists(std::span<std::unique_ptr<CommandList>> cmds) {
    std::vector<ID3D12CommandList*> directCmdLists;
    std::vector<ID3D12CommandList*> copyCmdLists;
    directCmdLists.reserve(cmds.size());
    copyCmdLists.reserve(cmds.size());

    // Track which allocator slots (per queue type) are being submitted
    bool gfxSlotsUsed[kMaxFrameLatency] = {};
    bool computeSlotsUsed[kMaxFrameLatency] = {};
    bool copySlotsUsed[kMaxFrameLatency] = {};

    for (auto& cmd : cmds) {
        cmd->close();

        if (auto* gfxCmd = dynamic_cast<GraphicsCommandListD3D12*>(cmd.get())) {
            directCmdLists.push_back(gfxCmd->m_list.Get());
            if (gfxCmd->m_allocatorSlot < kMaxFrameLatency)
                gfxSlotsUsed[gfxCmd->m_allocatorSlot] = true;
        } else if (auto* computeCmd = dynamic_cast<ComputeCommandListD3D12*>(cmd.get())) {
            directCmdLists.push_back(computeCmd->m_list.Get());
            if (computeCmd->m_allocatorSlot < kMaxFrameLatency)
                computeSlotsUsed[computeCmd->m_allocatorSlot] = true;
        } else if (auto* copyCmd = dynamic_cast<CopyCommandListD3D12*>(cmd.get())) {
            copyCmdLists.push_back(copyCmd->m_list.Get());
            if (copyCmd->m_allocatorSlot < kMaxFrameLatency)
                copySlotsUsed[copyCmd->m_allocatorSlot] = true;
        }
    }

    auto signal_fence = [&](ID3D12CommandQueue* queue) {
        m_fenceValue++;
        queue->Signal(m_fence.Get(), m_fenceValue);
        return m_fenceValue;
    };

    if (!directCmdLists.empty()) {
        m_directQueue->ExecuteCommandLists(
            static_cast<UINT>(directCmdLists.size()),
            directCmdLists.data());
        uint64_t val = signal_fence(m_directQueue.Get());
        for (UINT i = 0; i < kMaxFrameLatency; ++i) {
            if (gfxSlotsUsed[i])      m_cmdAllocatorFenceValues[i] = val;
            if (computeSlotsUsed[i])   m_cmdAllocatorFenceValuesCompute[i] = val;
        }
    }

    if (!copyCmdLists.empty()) {
        m_copyQueue->ExecuteCommandLists(
            static_cast<UINT>(copyCmdLists.size()),
            copyCmdLists.data());
        uint64_t val = signal_fence(m_copyQueue.Get());
        for (UINT i = 0; i < kMaxFrameLatency; ++i) {
            if (copySlotsUsed[i]) m_cmdAllocatorFenceValuesCopy[i] = val;
        }
    }
}

void DeviceD3D12::wait_for_idle() {
    m_fenceValue++;
    m_directQueue->Signal(m_fence.Get(), m_fenceValue);
    uint64_t directVal = m_fenceValue;

    m_fenceValue++;
    m_copyQueue->Signal(m_fence.Get(), m_fenceValue);
    uint64_t copyVal = m_fenceValue;

    uint64_t waitVal = (std::max)(directVal, copyVal);
    if (m_fence->GetCompletedValue() < waitVal) {
        HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (event) {
            m_fence->SetEventOnCompletion(waitVal, event);
            WaitForSingleObject(event, INFINITE);
            CloseHandle(event);
        }
    }
}

// === DescriptorAllocator ===
DescriptorAllocator::AllocResult DescriptorAllocator::alloc(UINT count) {
    if (currentOffset + count > capacity) {
        aether::log::error("DescriptorAllocator: out of descriptors ({}/{})",
                           currentOffset + count, capacity);
        return {{0}, {0}};
    }
    UINT offset = currentOffset;
    currentOffset += count;
    AllocResult result;
    result.cpu = {cpuStart.ptr + offset * descriptorSize};
    result.gpu = {gpuStart.ptr + offset * descriptorSize};
    return result;
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

void ShaderBindingD3D12::set_buffer(uint32_t slot, BufferPtr buffer, uint64_t offset, uint32_t stride) {
    if (slot >= m_descriptorCount) return;
    auto* buf = static_cast<BufferD3D12*>(buffer.get());
    if (!buf || !buf->resource) return;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = {
        m_cpuHandle.ptr + slot * m_descriptorSize
    };

    // Root signature maps: slot 0-13 = CBV b0-b13, slot 14-141 = SRV t0-t127, slot 142+ = UAV u0-u63
    if (slot < 14) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
        cbvDesc.BufferLocation = buf->resource->GetGPUVirtualAddress() + offset;
        cbvDesc.SizeInBytes = align_to(static_cast<UINT>(buf->desc.size), 256);
        m_device->CreateConstantBufferView(&cbvDesc, cpuHandle);
    } else if (slot < 14 + 128) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = static_cast<UINT>(offset / (stride > 0 ? stride : 4u));
        srvDesc.Buffer.NumElements = static_cast<UINT>(buf->desc.size / (stride > 0 ? stride : 4u));
        srvDesc.Buffer.StructureByteStride = stride > 0 ? stride : 4;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        m_device->CreateShaderResourceView(buf->resource.Get(), &srvDesc, cpuHandle);
    } else {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = static_cast<UINT>(offset / (stride > 0 ? stride : 4u));
        uavDesc.Buffer.NumElements = static_cast<UINT>(buf->desc.size / (stride > 0 ? stride : 4u));
        uavDesc.Buffer.StructureByteStride = stride > 0 ? stride : 4;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        m_device->CreateUnorderedAccessView(buf->resource.Get(), nullptr, &uavDesc, cpuHandle);
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

} // namespace aether::rhi
