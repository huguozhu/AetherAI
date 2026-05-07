module;
#include "d3d12_common.h"
module aether.rhi;

#include "device_d3d12_impl.h"
#include "pipeline_d3d12_impl.h"

import aether.core;
import <utility>;
import <memory>;
import <vector>;

namespace aether::rhi {

// === Device methods ===
GraphicsPipelinePtr DeviceD3D12::create_graphics_pipeline(const GfxPipelineDesc& desc) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

    // Shader bytecode
    if (!desc.vsBytecode.empty()) {
        psoDesc.VS = { desc.vsBytecode.data(), desc.vsBytecode.size() };
    }
    if (!desc.psBytecode.empty()) {
        psoDesc.PS = { desc.psBytecode.data(), desc.psBytecode.size() };
    }

    // Default state objects
    D3D12_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
    rasterizerDesc.FrontCounterClockwise = desc.frontCounterClockwise ? TRUE : FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.MultisampleEnable = FALSE;
    rasterizerDesc.AntialiasedLineEnable = FALSE;
    rasterizerDesc.ForcedSampleCount = 0;
    rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.RasterizerState = rasterizerDesc;

    D3D12_BLEND_DESC blendDesc{};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    for (UINT i = 0; i < 8; ++i) {
        blendDesc.RenderTarget[i].BlendEnable = FALSE;
        blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    psoDesc.BlendState = blendDesc;

    D3D12_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = (desc.dsvFormat != Format::Unknown);
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    dsDesc.StencilEnable = FALSE;
    psoDesc.DepthStencilState = dsDesc;

    // Helper to convert Format to DXGI format
    auto to_dxgi_format = [](Format fmt) -> DXGI_FORMAT {
        switch (fmt) {
            case Format::R8G8B8A8_UNORM:     return DXGI_FORMAT_R8G8B8A8_UNORM;
            case Format::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case Format::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case Format::R32_FLOAT:          return DXGI_FORMAT_R32_FLOAT;
            case Format::D32_FLOAT:          return DXGI_FORMAT_D32_FLOAT;
            case Format::D24_UNORM_S8_UINT:  return DXGI_FORMAT_D24_UNORM_S8_UINT;
            default:                         return DXGI_FORMAT_UNKNOWN;
        }
    };

    // RTV and DSV formats from descriptor
    psoDesc.RTVFormats[0] = to_dxgi_format(desc.rtvFormat);
    psoDesc.NumRenderTargets = desc.rtvCount;
    psoDesc.DSVFormat = to_dxgi_format(desc.dsvFormat);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.pRootSignature = m_rootSignature.Get();

    // Input layout for vertex attributes (POSITION, COLOR, TEXCOORD semantics)
    D3D12_INPUT_ELEMENT_DESC inputElements[3] = {};
    inputElements[0].SemanticName = "POSITION";
    inputElements[0].SemanticIndex = 0;
    inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[0].InputSlot = 0;
    inputElements[0].AlignedByteOffset = 0;
    inputElements[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElements[0].InstanceDataStepRate = 0;

    inputElements[1].SemanticName = "COLOR";
    inputElements[1].SemanticIndex = 0;
    inputElements[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputElements[1].InputSlot = 0;
    inputElements[1].AlignedByteOffset = 12; // after float3 position
    inputElements[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElements[1].InstanceDataStepRate = 0;

    inputElements[2].SemanticName = "TEXCOORD";
    inputElements[2].SemanticIndex = 0;
    inputElements[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputElements[2].InputSlot = 0;
    inputElements[2].AlignedByteOffset = 24; // after float3 position + float3 color
    inputElements[2].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    inputElements[2].InstanceDataStepRate = 0;

    if (desc.noInputLayout) {
        psoDesc.InputLayout = { nullptr, 0 };
    } else {
        psoDesc.InputLayout = { inputElements, 3 };
    }

    // Create PSO
    auto pipeline = std::make_shared<GraphicsPipelineD3D12>();
    HRESULT hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pso));

    if (FAILED(hr)) {
        aether::log::error("CreateGraphicsPipelineState failed with HRESULT: 0x{:08X}", static_cast<unsigned>(hr));

        // Capture D3D12 debug layer messages
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            UINT64 numMessages = infoQueue->GetNumStoredMessages();
            for (UINT64 i = 0; i < numMessages; ++i) {
                SIZE_T msgLen = 0;
                infoQueue->GetMessage(i, nullptr, &msgLen);
                if (msgLen > 0) {
                    std::vector<char> msgBuf(msgLen);
                    D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(msgBuf.data());
                    if (SUCCEEDED(infoQueue->GetMessage(i, msg, &msgLen))) {
                        aether::log::error("  D3D12 debug: {}", msg->pDescription);
                    }
                }
            }
            infoQueue->ClearStoredMessages();
        }

        // Check device removed reason
        HRESULT removedReason = m_device->GetDeviceRemovedReason();
        if (removedReason != S_OK) {
            aether::log::error("Device removed reason: 0x{:08X}", static_cast<unsigned>(removedReason));
        }

        // Report shader info
        aether::log::error("  VS bytecode: {} bytes | PS bytecode: {} bytes", desc.vsBytecode.size(), desc.psBytecode.size());
        aether::log::error("  RTV format: {} | DSV format: {} | RTV count: {} | Sample count: {}",
                           static_cast<int>(desc.rtvFormat),
                           static_cast<int>(desc.dsvFormat),
                           desc.rtvCount, psoDesc.SampleDesc.Count);
        aether::log::error("  Input elements: {} | Root sig: {}",
                           psoDesc.InputLayout.NumElements,
                           psoDesc.pRootSignature ? "custom" : "auto");
        return nullptr;
    }

    aether::log::info("Graphics pipeline created successfully");
    return pipeline;
}

ComputePipelinePtr DeviceD3D12::create_compute_pipeline(const ComputePipelineDesc& desc) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};

    if (!desc.csBytecode.empty()) {
        psoDesc.CS = { desc.csBytecode.data(), desc.csBytecode.size() };
    }
    psoDesc.pRootSignature = m_rootSignature.Get();

    auto pipeline = std::make_shared<ComputePipelineD3D12>();
    HRESULT hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pso));

    if (FAILED(hr)) {
        aether::log::error("Failed to create compute pipeline state");
        return nullptr;
    }

    return pipeline;
}

RayTracingPipelinePtr DeviceD3D12::create_ray_tracing_pipeline(const RTPipelineDesc& desc) {
    // TODO: Implement ray tracing pipeline state object
    aether::log::warn("Ray tracing pipeline not yet implemented");
    return nullptr;
}

} // namespace aether::rhi
