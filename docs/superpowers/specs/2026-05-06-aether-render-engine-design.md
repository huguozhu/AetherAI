# Aether 渲染引擎设计文档

## 概述

Aether 是一个跨平台渲染引擎，定位为研究学习项目，探索 GPU Driven Pipeline、C++20 Modules/Coroutine 等现代 C++ 技术。目标平台为 Windows（D3D12 为主），次要支持 Vulkan。

## 核心设计原则

- **C++20 Modules** 贯穿全项目，不出现传统头文件
- **严格单向依赖**：每一层只依赖下层，禁止反向依赖
- **最小外部依赖**：仅依赖必要的 SDK（Windows SDK, Vulkan SDK, Slang, 可选 Assimp）

---

## 1. 整体架构

### 1.1 模块层次

五个 CMake STATIC 库，严格分层：

```
Aether.Core       ← 基础类型、数学、内存、Log
Aether.RHI        ← CommandList 抽象，D3D12/Vulkan 实现
Aether.Resources  ← Coroutine 异步资源加载
Aether.Renderer   ← GPU Driven Pipeline
Aether.Shaders    ← Slang 编译与反射
```

### 1.2 C++20 Module 命名约定

| CMake Target | Module 名 | 导出内容 |
|---|---|---|
| Aether.Core | `aether.core` | Math, Memory, Span, Log, Handle, Task, ThreadPool |
| Aether.RHI | `aether.rhi` | Device, CommandList, Buffer, Texture, PipelineState, ShaderBinding, Fence, SwapChain |
| Aether.Resources | `aether.resources` | ResourceLoader, MeshResource, TextureResource, MaterialResource |
| Aether.Renderer | `aether.renderer` | Scene, SceneView, CullingJob, IndirectDraw, LODManager, GPUBufferManager |
| Aether.Shaders | `aether.shaders` | ShaderCompiler, ShaderLibrary, ShaderCompileResult |

### 1.3 构建系统

- CMake 3.28+（C++20 Modules 支持）
- Visual Studio 17.12+ (MSVC)
- Clang 18+ (可选)

---

## 2. Aether.Core

### 2.1 职责

提供所有上层依赖的基础设施，不包含任何 GPU 相关代码。

### 2.2 关键组件

| 组件 | 说明 |
|---|---|
| `Math` | float2/3/4, float4x4, Frustum, BoundingSphere |
| `Memory` | 对齐分配器、动态增长缓冲区 |
| `Span<T>` | C++20 span 封装 |
| `Log` | 简易日志系统 |
| `Handle<T>` | 类型安全的句柄（用于 GPU 资源索引） |
| `Task<T>` | C++20 Coroutine 原语，支持 co_await |

### 2.3 Task<T> 协程类型

```cpp
namespace aether::core {

template<typename T>
class Task {
    struct promise_type {
        // co_return / co_await 支持
    };
};

class ThreadPool {
    static ThreadPool& Get();
    void Enqueue(std::coroutine_handle<> handle);
};

// 工具
Task<std::vector<std::byte>> ReadFileAsync(std::string_view path);
Task<void> WhenAll(Task<T>... tasks);
Task<T> RunOnThreadPool(std::function<T()> fn);

} // namespace aether::core
```

---

## 3. Aether.RHI

### 3.1 职责

封装 D3D12/Vulkan，向上层提供统一的 CommandList 抽象接口。

### 3.2 Device 接口

```cpp
namespace aether::rhi {

class Device {
public:
    virtual ~Device() = default;

    virtual BufferPtr              CreateBuffer(const BufferDesc& desc) = 0;
    virtual TexturePtr             CreateTexture(const TextureDesc& desc) = 0;
    virtual GraphicsPipelinePtr    CreateGraphicsPipeline(const GfxPipelineDesc& desc) = 0;
    virtual ComputePipelinePtr     CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual RayTracingPipelinePtr  CreateRayTracingPipeline(const RTPipelineDesc& desc) = 0;
    virtual ShaderBindingPtr       CreateShaderBinding(const BindingLayout& layout) = 0;
    virtual FencePtr               CreateFence(uint64 initialValue = 0) = 0;
    virtual SwapChainPtr           CreateSwapChain(const SwapChainDesc& desc) = 0;

    virtual std::unique_ptr<GraphicsCommandList> CreateGraphicsCommandList() = 0;
    virtual std::unique_ptr<ComputeCommandList>  CreateComputeCommandList() = 0;
    virtual std::unique_ptr<CopyCommandList>     GetCopyQueue() = 0;

    virtual void ExecuteCommandLists(std::span<std::unique_ptr<CommandList>> cmds) = 0;
};

// Ptr 别名
using BufferPtr = std::shared_ptr<Buffer>;
using TexturePtr = std::shared_ptr<Texture>;
using GraphicsPipelinePtr = std::shared_ptr<GraphicsPipeline>;
using ComputePipelinePtr = std::shared_ptr<ComputePipeline>;
using RayTracingPipelinePtr = std::shared_ptr<RayTracingPipeline>;
using ShaderBindingPtr = std::shared_ptr<ShaderBinding>;
using FencePtr = std::shared_ptr<Fence>;
using SwapChainPtr = std::shared_ptr<SwapChain>;

} // namespace aether::rhi
```

- Device 生命周期：`std::unique_ptr<Device>`
- GPU 资源：`std::shared_ptr<T>`（引用计数管理）
- CommandList：`std::unique_ptr<T>`（短期分配，每帧重置）

### 3.3 CommandList 体系

- **GraphicsCommandList**: Draw, DrawIndexed, DrawIndirect, DispatchMesh, DispatchRays
- **ComputeCommandList**: Dispatch, DispatchIndirect
- **CopyCommandList**: 资源上传、拷贝

所有 CommandList 支持：ResourceBarrier, Clear, Copy, BindPipeline, BindDescriptor。

### 3.4 后端实现

| 特性 | D3D12 | Vulkan |
|---|---|---|
| 命令列表 | ID3D12GraphicsCommandList6 | VkCommandBuffer |
| 设备 | ID3D12Device10 | VkDevice |
| 描述符 | Descriptor Heap | Descriptor Set |
| Mesh Shader | ID3D12GraphicsCommandList::DispatchMesh | VK_EXT_mesh_shader |
| Ray Tracing | StateObject + DispatchRays | VK_KHR_ray_tracing_pipeline |

---

## 4. Aether.Resources

### 4.1 职责

基于 C++20 Coroutine 实现异步资源加载，不阻塞主线程。

### 4.2 ResourceLoader

```cpp
namespace aether::resources {

class ResourceLoader {
public:
    ResourceLoader(std::shared_ptr<rhi::Device> device, core::ThreadPool& pool);

    core::Task<MeshResource>     LoadMeshAsync(std::string_view path);
    core::Task<TextureResource>  LoadTextureAsync(std::string_view path);
    core::Task<MaterialResource> LoadMaterialAsync(std::string_view path);
};

struct MeshResource {
    rhi::BufferPtr vertexBuffer;
    rhi::BufferPtr indexBuffer;
    uint32_t vertexCount;
    uint32_t indexCount;
    std::vector<MeshLOD> lods;
};

struct TextureResource {
    rhi::TexturePtr texture;
    uint32_t width, height;
    rhi::Format format;
};

} // namespace aether::resources
```

### 4.3 加载管线

```
co_await ReadFileAsync(path)   → 后台线程读取文件
co_await RunOnThreadPool(decode) → 后台解码(Assimp/STB)
Device::CreateBuffer(...)      → RHI 线程创建 GPU 资源
CopyQueue::Upload(...)         → 上传到 GPU
Device::ExecuteCopyCommands()
co_return MeshResource
```

---

## 5. Aether.Renderer

### 5.1 职责

实现 GPU Driven Pipeline：GPU Culling、Indirect Rendering、GPU Data Management、Dynamic LOD。

### 5.2 帧循环

1. CPU 更新视锥体/相机数据
2. GPU Compute Dispatch：Culling + LOD 选择
3. Indirect Draw：读取 Culling 输出，压紧后执行间接绘制
4. Present

### 5.3 GPU 数据结构（持久储存在 GPU 缓冲区）

| 缓冲区 | 内容 |
|---|---|
| SceneBuffer | 物体世界矩阵、包围球、Mesh 索引、LOD 距离阈值 |
| MeshBuffer | 顶点/索引数据，每个物体的 4 级 LOD |
| MaterialBuffer | 材质参数、纹理句柄 |
| VisibleBuffer | Culling 输出标记（可见性 + LOD 级别） |
| IndirectBuffer | 压紧后的 DrawIndexedInstancedIndirect 参数 |
| LODBuffer | 当前每物体的 LOD 级别 |

### 5.4 GPU Culling 管线（Compute Shader）

- 视锥体裁剪（Frustum culling）
- 可选遮挡裁剪（Hi-Z Map）
- Dynamic LOD 选择（距离驱动 / 屏幕大小驱动）
- LOD 过渡使用 dither crossfade

### 5.5 Indirect Rendering

Culling 后执行可见性压紧（AppendBuffer/Counter），生成连续的 Indirect 参数列表，由 `DrawIndexedInstancedIndirect` 消费。

### 5.6 GPU Data Management

- 持久 GPU 缓冲区分配，无需每帧重建
- CPU 通过 Upload Buffer + Copy Queue 做增量更新
- 双缓冲防止 GPU 正在读取时 CPU 写入
- Bindless 描述符访问材质/Mesh 数据

---

## 6. Aether.Shaders

### 6.1 职责

封装 Khronos Slang 编译器，提供 Slang -> DXIL/SPIRV 编译、反射和缓存能力。

### 6.2 为什么选择 Slang

- 单一源码同时输出 DXIL（D3D12）和 SPIRV（Vulkan）
- 无需额外的 DXIL→SPIRV 转换步骤
- 内建反射系统（绑定、类型、EntryPoint）
- 原生支持 Ray Tracing 和 Mesh Shader
- 内置模块系统（import），支持 Shader 代码复用

### 6.3 核心接口

```cpp
namespace aether::shaders {

class ShaderCompiler {
public:
    bool Initialize();
    ShaderCompileResult Compile(const ShaderCompileDesc& desc);
};

class ShaderCompileResult {
public:
    bool IsValid() const;
    std::span<const std::byte> GetBytecode() const;
    slang::IShaderReflection* GetReflection() const;
    bool RequiresRayTracing() const;
    bool RequiresMeshShader() const;
};

class ShaderLibrary {
public:
    bool Add(std::string_view name, ShaderCompileResult);
    const ShaderCompileResult* Get(std::string_view name) const;
};

} // namespace aether::shaders
```

### 6.4 编译目标切换

```cpp
// D3D12
compiler.Compile({.entryPointName = "MainVS", .profile = "sm_6_7", .target = DXIL});

// Vulkan
compiler.Compile({.entryPointName = "MainVS", .profile = "spirv_1_6", .target = SPIRV});
```

---

## 7. 外部依赖清单

| 依赖 | 用途 | 可选/必需 |
|---|---|---|
| Windows SDK | D3D12, DXGI | 必需 (Windows) |
| Vulkan SDK | Vulkan API | 必需 (Vulkan) |
| libslang (slang.h) | Shader 编译 | 必需 |
| Assimp | 模型加载解码 | 可选 |

---

## 8. 项目目录结构

```
AetherAI/
├── CMakeLists.txt                 # 顶层 CMake
├── .gitignore
├── .superpowers/                  # 开发工具数据
├── docs/
│   └── superpowers/specs/
├── src/
│   ├── Aether.Core/
│   │   ├── CMakeLists.txt
│   │   ├── module/                # .ixx 模块接口文件
│   │   └── src/                   # .cpp 实现文件
│   ├── Aether.RHI/
│   │   ├── CMakeLists.txt
│   │   ├── module/
│   │   ├── src/                   # 抽象基类实现
│   │   ├── D3D12/                 # D3D12 后端
│   │   └── Vulkan/                # Vulkan 后端
│   ├── Aether.Resources/
│   │   ├── CMakeLists.txt
│   │   ├── module/
│   │   └── src/
│   ├── Aether.Renderer/
│   │   ├── CMakeLists.txt
│   │   ├── module/
│   │   └── src/
│   └── Aether.Shaders/
│       ├── CMakeLists.txt
│       ├── module/
│       └── src/
└── examples/                      # 示例程序
    └── triangle/
```
