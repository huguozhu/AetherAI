#pragma once
#include "d3d12_common.h"

namespace aether::rhi {

struct GraphicsPipelineD3D12 : public GraphicsPipeline {
    ComPtr<ID3D12PipelineState> pso;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    inline PipelineType get_type() const override { return PipelineType::Graphics; }
};

struct ComputePipelineD3D12 : public ComputePipeline {
    ComPtr<ID3D12PipelineState> pso;

    inline PipelineType get_type() const override { return PipelineType::Compute; }
};

} // namespace aether::rhi
