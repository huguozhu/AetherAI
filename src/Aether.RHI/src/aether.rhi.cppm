module;
#include <memory>
#include <vector>
#include <cstdint>
#include <span>
#include <string>

export module aether.rhi;

import aether.core;

export namespace aether::rhi {

// === Enums ===
enum class Format : uint8_t { Unknown, R8G8B8A8_UNORM, R16G16B16A16_FLOAT, R32G32B32A32_FLOAT, R32_FLOAT, D32_FLOAT, D24_UNORM_S8_UINT };
enum class HeapType : uint8_t { Default, Upload, Readback };
enum class BindFlags : uint8_t { None = 0, VertexBuffer = 1, IndexBuffer = 2, ConstantBuffer = 4, ShaderResource = 8, UnorderedAccess = 16 };
enum class PipelineType : uint8_t { Graphics, Compute, RayTracing };
enum class ResourceState : uint8_t {
    Common,
    RenderTarget,
    CopySource,
    CopyDest,
    VertexAndConstantBuffer,
    IndexBuffer,
    UnorderedAccess,
    DepthStencilWrite,
    DepthStencilRead,
    ShaderResource,
    IndirectArgument,
    GenericRead,
    StreamOut,
    Predication,
};

// === Descriptors ===
struct BufferDesc {
    uint64_t size = 0;
    HeapType heap = HeapType::Default;
    BindFlags bindFlags = BindFlags::None;
    uint32_t structureStride = 0;
};

struct TextureDesc {
    uint32_t width = 0, height = 0, depth = 1;
    uint32_t mipLevels = 1;
    Format format = Format::R8G8B8A8_UNORM;
};

struct GfxPipelineDesc {
    std::span<const std::byte> vsBytecode;
    std::span<const std::byte> psBytecode;
    std::span<const std::byte> msBytecode;
    std::span<const std::byte> asBytecode;
    Format rtvFormat = Format::R8G8B8A8_UNORM;
    Format dsvFormat = Format::D32_FLOAT;
    uint32_t rtvCount = 1;
    bool noInputLayout = false; // set true when VS uses SV_VertexID/SV_InstanceID only
    bool frontCounterClockwise = false; // triangle winding (false = CW front, true = CCW front)
};

struct ComputePipelineDesc {
    std::span<const std::byte> csBytecode;
};

struct RTPipelineDesc {
    std::span<const std::byte> libraryBytecode;
    std::vector<std::string> exportedSymbols;
    uint32_t maxPayloadSize = 32;
    uint32_t maxAttributeSize = 8;
};

struct SwapChainDesc {
    void* windowHandle = nullptr;
    uint32_t width = 1280, height = 720;
    Format format = Format::R8G8B8A8_UNORM;
    uint32_t bufferCount = 3;
};

struct BindingLayout {
    uint32_t numDescriptors = 0;
};

// === Ptr aliases ===
class Buffer; using BufferPtr = std::shared_ptr<Buffer>;
class Texture; using TexturePtr = std::shared_ptr<Texture>;
class GraphicsPipeline; using GraphicsPipelinePtr = std::shared_ptr<GraphicsPipeline>;
class ComputePipeline; using ComputePipelinePtr = std::shared_ptr<ComputePipeline>;
class RayTracingPipeline; using RayTracingPipelinePtr = std::shared_ptr<RayTracingPipeline>;
class ShaderBinding; using ShaderBindingPtr = std::shared_ptr<ShaderBinding>;
class Fence; using FencePtr = std::shared_ptr<Fence>;
class SwapChain; using SwapChainPtr = std::shared_ptr<SwapChain>;

// === Resource base ===
class Resource {
public:
    virtual ~Resource() = default;
    virtual uint64_t get_gpu_address() const = 0;
};

class Buffer : public Resource {
public:
    virtual BufferDesc get_desc() const = 0;
    virtual void* map() = 0;
    virtual void unmap() = 0;
};

class Texture : public Resource {
public:
    virtual TextureDesc get_desc() const = 0;
};

// === Pipeline State ===
class PipelineState {
public:
    virtual ~PipelineState() = default;
    virtual PipelineType get_type() const = 0;
};

class GraphicsPipeline : public PipelineState {};
class ComputePipeline : public PipelineState {};
class RayTracingPipeline : public PipelineState {};

// === ShaderBinding ===
class ShaderBinding {
public:
    virtual ~ShaderBinding() = default;
    virtual void set_buffer(uint32_t slot, BufferPtr buffer, uint64_t offset = 0, uint32_t stride = 0) = 0;
    virtual void set_texture(uint32_t slot, TexturePtr texture) = 0;
};

// === Fence ===
class Fence {
public:
    virtual ~Fence() = default;
    virtual uint64_t get_value() const = 0;
    virtual void signal(uint64_t value) = 0;
    virtual void wait(uint64_t value) = 0;
};

// === SwapChain ===
class SwapChain {
public:
    virtual ~SwapChain() = default;
    virtual uint32_t get_current_index() const = 0;
    virtual TexturePtr get_back_buffer(uint32_t index) = 0;
    virtual void present(bool vsync = true) = 0;
    virtual void resize(uint32_t width, uint32_t height) = 0;
};

// === CommandList ===
class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void reset() = 0;
    virtual void close() = 0;

    virtual void resource_barrier(Resource* resource, ResourceState stateBefore, ResourceState stateAfter) = 0;
    virtual void clear_texture(Texture* texture, const float color[4]) = 0;
    virtual void clear_depth(Texture* texture, float depth, uint8_t stencil) = 0;
    virtual void bind_pipeline(PipelineState* pipeline) = 0;
    virtual void bind_descriptor(uint32_t slot, ShaderBinding* binding) = 0;
};

class GraphicsCommandList : public CommandList {
public:
    virtual void set_viewport(float x, float y, float w, float h, float minDepth = 0, float maxDepth = 1) = 0;
    virtual void set_scissor(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0;
    virtual void ia_set_vertex_buffer(uint32_t slot, Buffer* buffer, uint32_t stride) = 0;
    virtual void ia_set_index_buffer(Buffer* buffer, Format format = Format::R32_FLOAT) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0, uint32_t startInstance = 0) = 0;
    virtual void draw_indexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndex = 0, int32_t baseVertex = 0, uint32_t startInstance = 0) = 0;
    virtual void draw_indirect(Buffer* args, uint32_t offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void bind_render_targets(Texture* rtv, Texture* dsv = nullptr) = 0;
    virtual void dispatch_mesh(uint32_t groupX, uint32_t groupY, uint32_t groupZ) = 0;
    virtual void dispatch_rays(const void* rayGenShaderTable, const void* missShaderTable, const void* hitGroupTable, uint32_t width, uint32_t height, uint32_t depth) = 0;
};

class ComputeCommandList : public CommandList {
public:
    virtual void dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) = 0;
    virtual void dispatch_indirect(Buffer* args, uint32_t offset) = 0;
};

class CopyCommandList : public CommandList {
public:
    virtual void upload_buffer(Buffer* dst, uint64_t dstOffset, const void* src, size_t size) = 0;
    virtual void upload_texture(Texture* dst, const void* src, size_t size) = 0;
};

// === Device ===
class Device {
public:
    virtual ~Device() = default;

    virtual BufferPtr              create_buffer(const BufferDesc& desc) = 0;
    virtual TexturePtr             create_texture(const TextureDesc& desc) = 0;
    virtual GraphicsPipelinePtr    create_graphics_pipeline(const GfxPipelineDesc& desc) = 0;
    virtual ComputePipelinePtr     create_compute_pipeline(const ComputePipelineDesc& desc) = 0;
    virtual RayTracingPipelinePtr  create_ray_tracing_pipeline(const RTPipelineDesc& desc) = 0;
    virtual ShaderBindingPtr       create_shader_binding(const BindingLayout& layout) = 0;
    virtual FencePtr               create_fence(uint64_t initialValue = 0) = 0;
    virtual SwapChainPtr           create_swap_chain(const SwapChainDesc& desc) = 0;

    virtual std::unique_ptr<GraphicsCommandList> create_graphics_command_list() = 0;
    virtual std::unique_ptr<ComputeCommandList>  create_compute_command_list() = 0;
    virtual std::unique_ptr<CopyCommandList>     get_copy_queue() = 0;

    virtual void execute_command_lists(std::span<std::unique_ptr<CommandList>> cmds) = 0;
    virtual void wait_for_idle() = 0;
};

// Factory
std::unique_ptr<Device> create_d3d12_device();

} // namespace aether::rhi
