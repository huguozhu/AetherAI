# Aether Render Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a cross-platform rendering engine with C++20 Modules, CommandList RHI, GPU Driven Pipeline, async resource loading, and Slang shading language.

**Architecture:** 5-layer CMake library stack (Core → RHI → Resources → Renderer → Shaders) with strict one-way dependencies. Each layer is a STATIC library using C++20 Modules for all interfaces.

**Tech Stack:** CMake 4.3, MSVC v14.44 (VS 2022 17.12+), C++20, D3D12, Slang, Assimp (optional)

---

## Phase 1: Project Scaffolding + Aether.Core

### Task 1: Create top-level CMake + .gitignore

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Modify: `.gitignore`
- Create: `src/Aether.Core/CMakeLists.txt`

- [ ] **Step 1: Write top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.28)
project(AetherAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable C++20 Modules support
if(MSVC)
    set(CMAKE_MSVC_FLAGS "/std:c++20 /Zc:preprocessor /experimental:module")
endif()

add_subdirectory(src/Aether.Core)
add_subdirectory(src/Aether.RHI)
add_subdirectory(src/Aether.Resources)
add_subdirectory(src/Aether.Renderer)
add_subdirectory(src/Aether.Shaders)
```

- [ ] **Step 2: Write Aether.Core CMakeLists.txt**

```cmake
add_library(Aether.Core STATIC)

target_sources(Aether.Core
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.core.cppm
    PRIVATE
        src/math_impl.cpp
        src/thread_pool.cpp
)

target_include_directories(Aether.Core PUBLIC src)
```

- [ ] **Step 3: Write .gitignore**

```
build/
.vs/
*.user
*.suo
.superpowers/
```

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt CMakePresets.json .gitignore src/Aether.Core/CMakeLists.txt
git commit -m "feat: initial CMake project scaffolding"
```

### Task 2: Aether.Core module interface — core types

**Files:**
- Create: `src/Aether.Core/src/aether.core.cppm`

- [ ] **Step 1: Write the primary Core module interface**

```cpp
export module aether.core;

// === Math Types ===
export namespace aether::math {

struct float2 { float x, y; };
struct float3 { float x, y, z; };
struct float4 { float x, y, z, w; };

struct float4x4 {
    float m[4][4];
    static float4x4 identity();
    static float4x4 perspective(float fovY, float aspect, float nearZ, float farZ);
    static float4x4 look_at(float3 eye, float3 target, float3 up);
    static float4x4 transpose(const float4x4& mat);
    float4x4 operator*(const float4x4& rhs) const;
};

struct BoundingSphere {
    float3 center;
    float radius;
};

struct Frustum {
    float4 planes[6]; // left, right, top, bottom, near, far — each as (nx,ny,nz,d)
    static Frustum from_view_proj(const float4x4& viewProj);
    bool contains(const BoundingSphere& sphere) const;
};

} // namespace aether::math

// === Span ===
export namespace aether {
template<typename T>
class Span {
public:
    Span() = default;
    Span(T* data, size_t size) : m_data(data), m_size(size) {}
    T* data() const { return m_data; }
    size_t size() const { return m_size; }
    T& operator[](size_t i) const { return m_data[i]; }
    T* begin() const { return m_data; }
    T* end() const { return m_data + m_size; }
private:
    T* m_data = nullptr;
    size_t m_size = 0;
};
} // namespace aether

// === Handle ===
export namespace aether {

template<typename T, typename IndexType = uint32_t>
struct Handle {
    IndexType index = IndexType(-1);
    bool is_valid() const { return index != IndexType(-1); }
    explicit operator bool() const { return is_valid(); }
};

} // namespace aether

// === Log ===
export namespace aether::log {

enum class Level { Debug, Info, Warn, Error };

void write(Level level, std::string_view message);

template<typename... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace aether::log

// === Memory ===
export namespace aether::memory {

// Aligned allocation for GPU data
void* allocate_aligned(size_t size, size_t alignment = 256);
void  free_aligned(void* ptr);

// Dynamic growing buffer (for staging data)
template<typename T>
class DynamicBuffer {
public:
    DynamicBuffer() = default;
    ~DynamicBuffer() { delete[] m_data; }

    void append(const T& item) {
        if (m_count >= m_capacity) grow();
        m_data[m_count++] = item;
    }

    T* data() const { return m_data; }
    size_t size() const { return m_count; }

private:
    void grow() {
        m_capacity = m_capacity ? m_capacity * 2 : 64;
        auto* newData = new T[m_capacity];
        for (size_t i = 0; i < m_count; ++i) newData[i] = m_data[i];
        delete[] m_data;
        m_data = newData;
    }
    T* m_data = nullptr;
    size_t m_count = 0;
    size_t m_capacity = 0;
};

} // namespace aether::memory

// === Task Coroutine (forward declaration) ===
export namespace aether::concurrency {

class ThreadPool;

template<typename T>
class Task;

// Promise type for Task<T>
template<typename T>
struct TaskPromise {
    std::coroutine_handle<> m_continuation;
    std::exception_ptr m_exception;
    std::optional<T> m_result;

    Task<T> get_return_object();
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { m_exception = std::current_exception(); }
    template<typename U> requires std::convertible_to<U&&, T>
    void return_value(U&& value) { m_result = std::forward<U>(value); }
};

template<>
struct TaskPromise<void> {
    // void specialization
};

template<typename T>
class [[nodiscard]] Task {
public:
    using promise_type = TaskPromise<T>;
    Task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    ~Task() { if (m_handle) m_handle.destroy(); }
    Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Task& operator=(Task&&) = delete;
    Task(const Task&) = delete;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> awaiting) noexcept {
        m_handle.promise().m_continuation = awaiting;
        ThreadPool::global().enqueue(m_handle);
    }
    T await_resume() {
        if (m_handle.promise().m_exception) std::rethrow_exception(m_handle.promise().m_exception);
        return std::move(*m_handle.promise().m_result);
    }

private:
    std::coroutine_handle<promise_type> m_handle;
};

// ThreadPool
export class ThreadPool {
public:
    static ThreadPool& global();

    ThreadPool(size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPool();

    void enqueue(std::coroutine_handle<> handle);
    void enqueue(std::function<void()> task);

private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
};

// Utility: ReadFileAsync
export Task<std::vector<std::byte>> read_file_async(std::string_view path);

// Utility: WhenAll
template<typename... Ts>
Task<std::tuple<Ts...>> when_all(Task<Ts>... tasks);

} // namespace aether::concurrency
```

- [ ] **Step 2: Commit**

```bash
git add src/Aether.Core/src/aether.core.cppm
git commit -m "feat: add Aether.Core module interface"
```

### Task 3: Aether.Core math implementation

**Files:**
- Create: `src/Aether.Core/src/math_impl.cpp`

- [ ] **Step 1: Write math implementation**

```cpp
module aether.core;

namespace aether::math {

float4x4 float4x4::identity() {
    return float4x4{{
        {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
    }};
}

float4x4 float4x4::perspective(float fovY, float aspect, float nearZ, float farZ) {
    float yScale = 1.0f / std::tan(fovY * 0.5f);
    float xScale = yScale / aspect;
    return float4x4{{
        {xScale,0,0,0}, {0,yScale,0,0}, {0,0,farZ/(farZ-nearZ),1}, {0,0,-nearZ*farZ/(farZ-nearZ),0}
    }};
}

float4x4 float4x4::look_at(float3 eye, float3 target, float3 up) {
    // Standard look-at matrix
    float3 f = {target.x - eye.x, target.y - eye.y, target.z - eye.z};
    float fLen = std::sqrt(f.x*f.x + f.y*f.y + f.z*f.z);
    f = {f.x/fLen, f.y/fLen, f.z/fLen};
    float3 s = {f.y*up.z - f.z*up.y, f.z*up.x - f.x*up.z, f.x*up.y - f.y*up.x};
    float sLen = std::sqrt(s.x*s.x + s.y*s.y + s.z*s.z);
    s = {s.x/sLen, s.y/sLen, s.z/sLen};
    float3 u = {s.y*f.z - s.z*f.y, s.z*f.x - s.x*f.z, s.x*f.y - s.y*f.x};
    return float4x4{{
        {s.x, u.x, -f.x, 0},
        {s.y, u.y, -f.y, 0},
        {s.z, u.z, -f.z, 0},
        {-s.x*eye.x - s.y*eye.y - s.z*eye.z,
         -u.x*eye.x - u.y*eye.y - u.z*eye.z,
         f.x*eye.x + f.y*eye.y + f.z*eye.z, 1}
    }};
}

float4x4 float4x4::transpose(const float4x4& mat) {
    float4x4 result;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            result.m[i][j] = mat.m[j][i];
    return result;
}

float4x4 float4x4::operator*(const float4x4& rhs) const {
    float4x4 result{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                result.m[i][j] += m[i][k] * rhs.m[k][j];
    return result;
}

Frustum Frustum::from_view_proj(const float4x4& vp) {
    Frustum f;
    // Extract frustum planes from VP matrix
    // left:   m3 + m0, right:   m3 - m0
    // top:    m3 - m1, bottom:  m3 + m1
    // near:   m3 + m2, far:     m3 - m2
    auto& m = vp.m;
    auto extract_plane = [](auto& row) -> float4 {
        float len = std::sqrt(row[0]*row[0] + row[1]*row[1] + row[2]*row[2]);
        return {row[0]/len, row[1]/len, row[2]/len, row[3]/len};
    };
    float4 rows[6] = {
        {m[0][3]+m[0][0], m[1][3]+m[1][0], m[2][3]+m[2][0], m[3][3]+m[3][0]}, // left
        {m[0][3]-m[0][0], m[1][3]-m[1][0], m[2][3]-m[2][0], m[3][3]-m[3][0]}, // right
        {m[0][3]-m[0][1], m[1][3]-m[1][1], m[2][3]-m[2][1], m[3][3]-m[3][1]}, // top
        {m[0][3]+m[0][1], m[1][3]+m[1][1], m[2][3]+m[2][1], m[3][3]+m[3][1]}, // bottom
        {m[0][3]+m[0][2], m[1][3]+m[1][2], m[2][3]+m[2][2], m[3][3]+m[3][2]}, // near
        {m[0][3]-m[0][2], m[1][3]-m[1][2], m[2][3]-m[2][2], m[3][3]-m[3][2]}, // far
    };
    for (int i = 0; i < 6; ++i) f.planes[i] = extract_plane(rows[i]);
    return f;
}

bool Frustum::contains(const BoundingSphere& sphere) const {
    for (const auto& plane : planes) {
        float dot = plane.x * sphere.center.x + plane.y * sphere.center.y
                  + plane.z * sphere.center.z + plane.w;
        if (dot < -sphere.radius) return false;
    }
    return true;
}

} // namespace aether::math
```

- [ ] **Step 2: Commit**

```bash
git add src/Aether.Core/src/math_impl.cpp
git commit -m "feat: implement Aether.Core math types"
```

### Task 4: Aether.Core ThreadPool + Log implementation

**Files:**
- Create: `src/Aether.Core/src/thread_pool.cpp`
- Create: `src/Aether.Core/src/log_impl.cpp`
- Create: `src/Aether.Core/src/read_file_async.cpp`

- [ ] **Step 1: Write ThreadPool + Log implementation**

`src/Aether.Core/src/log_impl.cpp`:
```cpp
module aether.core;

namespace aether::log {

void write(Level level, std::string_view message) {
    static const char* levelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << "[" << levelNames[static_cast<int>(level)] << "] "
              << std::put_time(std::localtime(&now), "%H:%M:%S") << " "
              << message << std::endl;
}

} // namespace aether::log
```

`src/Aether.Core/src/thread_pool.cpp`:
```cpp
module aether.core;

namespace aether::concurrency {

ThreadPool& ThreadPool::global() {
    static ThreadPool pool;
    return pool;
}

ThreadPool::ThreadPool(size_t threadCount) : m_stop(false) {
    for (size_t i = 0; i < threadCount; ++i) {
        m_workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock lock(m_mutex);
                    m_cv.wait(lock, [this] { return m_stop || !m_tasks.empty(); });
                    if (m_stop && m_tasks.empty()) return;
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& worker : m_workers) worker.join();
}

void ThreadPool::enqueue(std::coroutine_handle<> handle) {
    enqueue([handle]() mutable { handle.resume(); });
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard lock(m_mutex);
        m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
}

Task<std::vector<std::byte>> read_file_async(std::string_view path) {
    // Will be implemented with std::ifstream
    auto& pool = ThreadPool::global();
    // Launch file read on thread pool
    // ...
    co_return std::vector<std::byte>{};
}

} // namespace aether::concurrency
```

- [ ] **Step 2: Build and fix errors**

```bash
mkdir -p build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --target Aether.Core
```

- [ ] **Step 3: Commit**

```bash
git add src/Aether.Core/src/
git commit -m "feat: implement ThreadPool, Log, read_file_async"
```

---

## Phase 2: Aether.RHI (D3D12 Primary)

### Task 5: Aether.RHI module interface — abstract types

**Files:**
- Create: `src/Aether.RHI/CMakeLists.txt`
- Create: `src/Aether.RHI/src/aether.rhi.cppm`

- [ ] **Step 1: Write RHI CMakeLists.txt**

```cmake
add_library(Aether.RHI STATIC)

target_sources(Aether.RHI
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.rhi.cppm
    PRIVATE
        D3D12/device_d3d12.cpp
        D3D12/command_list_d3d12.cpp
        D3D12/resource_d3d12.cpp
        D3D12/pipeline_d3d12.cpp
        D3D12/swapchain_d3d12.cpp
)

target_link_libraries(Aether.RHI PUBLIC Aether.Core)

target_include_directories(Aether.RHI PUBLIC src)
```

- [ ] **Step 2: Write the RHI module interface**

```cpp
export module aether.rhi;

import aether.core;

export namespace aether::rhi {

// === Forward declarations ===
enum class Format : uint8_t {
    Unknown,
    R8G8B8A8_UNORM,
    R16G16B16A16_FLOAT,
    R32G32B32A32_FLOAT,
    R32_FLOAT,
    D32_FLOAT,
    D24_UNORM_S8_UINT,
};

enum class HeapType : uint8_t { Default, Upload, Readback };

enum class BindFlags : uint8_t {
    None = 0,
    VertexBuffer = 1,
    IndexBuffer  = 2,
    ConstantBuffer = 4,
    ShaderResource = 8,
    UnorderedAccess = 16,
};

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

enum class PipelineType : uint8_t {
    Graphics,
    Compute,
    RayTracing,
};

enum class ShaderType : uint8_t {
    Vertex, Pixel, Compute, Mesh, Amplification,
    RayGen, Miss, ClosestHit, AnyHit, Intersection, Callable,
};

// === Classes ===
class Buffer;
using BufferPtr = std::shared_ptr<Buffer>;

class Texture;
using TexturePtr = std::shared_ptr<Texture>;

class GraphicsPipeline;
using GraphicsPipelinePtr = std::shared_ptr<GraphicsPipeline>;

class ComputePipeline;
using ComputePipelinePtr = std::shared_ptr<ComputePipeline>;

class RayTracingPipeline;
using RayTracingPipelinePtr = std::shared_ptr<RayTracingPipeline>;

class ShaderBinding;
using ShaderBindingPtr = std::shared_ptr<ShaderBinding>;

class Fence;
using FencePtr = std::shared_ptr<Fence>;

class SwapChain;
using SwapChainPtr = std::shared_ptr<SwapChain>;

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

// === Pipeline ===
struct GfxPipelineDesc {
    std::vector<std::byte> vsBytecode;
    std::vector<std::byte> psBytecode;
    std::vector<std::byte> msBytecode;   // Mesh shader
    std::vector<std::byte> asBytecode;   // Amplification shader
    Format rtvFormat = Format::R8G8B8A8_UNORM;
    Format dsvFormat = Format::D32_FLOAT;
};

struct RTPipelineDesc {
    std::vector<std::byte> libraryBytecode;
    std::vector<std::string> exportedSymbols;
    uint32_t maxPayloadSize = 32;
    uint32_t maxAttributeSize = 8;
};

struct BindingLayout {
    // Describes binding slots
};

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
    virtual void set_buffer(uint32_t slot, BufferPtr buffer, uint64_t offset = 0) = 0;
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
struct SwapChainDesc {
    void* windowHandle = nullptr;
    uint32_t width = 1280, height = 720;
    Format format = Format::R8G8B8A8_UNORM;
    uint32_t bufferCount = 3;
};

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

    virtual void resource_barrier(Resource* resource, uint32_t stateBefore, uint32_t stateAfter) = 0;
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
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t startVertex = 0, uint32_t startInstance = 0) = 0;
    virtual void draw_indexed(uint32_t indexCount, uint32_t instanceCount = 1,
                              uint32_t startIndex = 0, int32_t baseVertex = 0,
                              uint32_t startInstance = 0) = 0;
    virtual void draw_indirect(Buffer* args, uint32_t offset, uint32_t drawCount, uint32_t stride) = 0;
    virtual void dispatch_mesh(uint32_t groupX, uint32_t groupY, uint32_t groupZ) = 0;
    virtual void dispatch_rays(void* rayGenShaderTable, void* missShaderTable,
                               void* hitGroupTable, uint32_t width, uint32_t height, uint32_t depth) = 0;
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

// Factory function
std::unique_ptr<Device> create_d3d12_device();

} // namespace aether::rhi
```

- [ ] **Step 3: Commit**

```bash
git add src/Aether.RHI/
git commit -m "feat: add Aether.RHI abstract interface module"
```

### Task 6: D3D12 Device implementation (partial)

**Files:**
- Create: `src/Aether.RHI/D3D12/device_d3d12.cpp`
- Create: `src/Aether.RHI/D3D12/d3d12_common.h` (internal helpers, NOT exported module)

- [ ] **Step 1: Write D3D12 internal headers**

`src/Aether.RHI/D3D12/d3d12_common.h`:
```cpp
#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <span>
#include <memory>
#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;
```

- [ ] **Step 2: Write DeviceD3D12**

```cpp
module aether.rhi;

import :d3d12_common;

namespace aether::rhi {

class DeviceD3D12 : public Device {
public:
    DeviceD3D12();
    ~DeviceD3D12() override;

    // Device interface
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

    // Internal accessors
    ID3D12Device10* d3d_device() const { return m_device.Get(); }

private:
    void initialize();
    void create_factory();
    void create_adapter();
    void create_direct_queue();
    void create_copy_queue();

    ComPtr<IDXGIFactory6>    m_factory;
    ComPtr<ID3D12Device10>   m_device;
    ComPtr<ID3D12CommandQueue> m_directQueue;
    ComPtr<ID3D12CommandQueue> m_copyQueue;
    ComPtr<ID3D12CommandAllocator> m_cmdAllocators[3];
    ComPtr<ID3D12Fence>      m_fence;
    uint64_t                 m_fenceValue = 0;
};

// Factory
std::unique_ptr<Device> create_d3d12_device() {
    return std::make_unique<DeviceD3D12>();
}

DeviceD3D12::DeviceD3D12() {
    initialize();
}

DeviceD3D12::~DeviceD3D12() {
    wait_for_idle();
}

void DeviceD3D12::initialize() {
    create_factory();
    create_adapter();
    create_direct_queue();
    create_copy_queue();

    // Create fence
    D3D12_FENCE_DESC fenceDesc{};
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
}

void DeviceD3D12::create_factory() {
    UINT flags = 0;
#ifdef _DEBUG
    flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
}

void DeviceD3D12::create_adapter() {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                        IID_PPV_ARGS(&m_device)))) {
            break;
        }
    }
}

void DeviceD3D12::create_direct_queue() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_directQueue));
}

void DeviceD3D12::create_copy_queue() {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_copyQueue));
}
```

- [ ] **Step 3: Commit**

```bash
git add src/Aether.RHI/D3D12/d3d12_common.h src/Aether.RHI/D3D12/device_d3d12.cpp
git commit -m "feat: implement D3D12 device with factory and queues"
```

### Task 7: D3D12 Buffer/Texture resources

**Files:**
- Create: `src/Aether.RHI/D3D12/resource_d3d12.cpp`

- [ ] **Step 1: Write BufferD3D12 + TextureD3D12**

```cpp
module aether.rhi;

struct BufferD3D12 : public Buffer {
    ComPtr<ID3D12Resource> resource;
    BufferDesc desc;

    BufferDesc get_desc() const override { return desc; }

    uint64_t get_gpu_address() const override {
        return resource->GetGPUVirtualAddress();
    }

    void* map() override {
        void* data;
        resource->Map(0, nullptr, &data);
        return data;
    }

    void unmap() override {
        resource->Unmap(0, nullptr);
    }
};

struct TextureD3D12 : public Texture {
    ComPtr<ID3D12Resource> resource;
    TextureDesc desc;

    TextureDesc get_desc() const override { return desc; }

    uint64_t get_gpu_address() const override {
        return resource->GetGPUVirtualAddress();
    }
};

// Device implementation
BufferPtr DeviceD3D12::create_buffer(const BufferDesc& desc) {
    auto buffer = std::make_shared<BufferD3D12>();
    buffer->desc = desc;

    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

    switch (desc.heap) {
        case HeapType::Upload:
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            state = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case HeapType::Readback:
            heapType = D3D12_HEAP_TYPE_READBACK;
            state = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
        default:
            heapType = D3D12_HEAP_TYPE_DEFAULT;
            state = D3D12_RESOURCE_STATE_COMMON;
            break;
    }

    if (desc.bindFlags & BindFlags::UnorderedAccess)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = heapType;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = desc.size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = flags;

    HRESULT hr = m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, state, nullptr,
        IID_PPV_ARGS(&buffer->resource));
    // TODO: handle error

    return buffer;
}

TexturePtr DeviceD3D12::create_texture(const TextureDesc& desc) {
    auto texture = std::make_shared<TextureD3D12>();
    texture->desc = desc;

    auto dxgiFormat = static_cast<DXGI_FORMAT>(
        desc.format == Format::R8G8B8A8_UNORM ? DXGI_FORMAT_R8G8B8A8_UNORM :
        desc.format == Format::R32G32B32A32_FLOAT ? DXGI_FORMAT_R32G32B32A32_FLOAT :
        desc.format == Format::D32_FLOAT ? DXGI_FORMAT_D32_FLOAT :
        DXGI_FORMAT_UNKNOWN);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = desc.depth;
    resourceDesc.MipLevels = desc.mipLevels;
    resourceDesc.Format = dxgiFormat;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&texture->resource));

    return texture;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/Aether.RHI/D3D12/resource_d3d12.cpp
git commit -m "feat: implement D3D12 buffer and texture resources"
```

### Task 8: D3D12 CommandList implementations

**Files:**
- Create: `src/Aether.RHI/D3D12/command_list_d3d12.cpp`

- [ ] **Step 1: Write GraphicsCommandListD3D12 + ComputeCommandListD3D12 + CopyCommandListD3D12**

```cpp
module aether.rhi;

struct GraphicsCommandListD3D12 : public GraphicsCommandList {
    ComPtr<ID3D12GraphicsCommandList6> m_list;
    ComPtr<ID3D12CommandAllocator> m_allocator;

    void reset() override {
        m_allocator->Reset();
        m_list->Reset(m_allocator.Get(), nullptr);
    }

    void close() override { m_list->Close(); }

    void resource_barrier(Resource* resource, uint32_t stateBefore, uint32_t stateAfter) override {
        auto* buffer = dynamic_cast<BufferD3D12*>(resource);
        auto* tex = dynamic_cast<TextureD3D12*>(resource);
        ID3D12Resource* res = buffer ? buffer->resource.Get() : tex->resource.Get();

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = res;
        barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(stateBefore);
        barrier.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(stateAfter);
        m_list->ResourceBarrier(1, &barrier);
    }

    void clear_texture(Texture* texture, const float color[4]) override {
        auto* tex = static_cast<TextureD3D12*>(texture);
        // Use descriptor heap handle for RTV
        m_list->ClearRenderTargetView({}, color, 0, nullptr);
    }

    void bind_pipeline(PipelineState* pipeline) override {
        auto* gfxPipeline = dynamic_cast<GraphicsPipeline*>(pipeline);
        // Set PSO on command list
    }

    void set_viewport(float x, float y, float w, float h, float minDepth, float maxDepth) override {
        D3D12_VIEWPORT vp{x, y, w, h, minDepth, maxDepth};
        m_list->RSSetViewports(1, &vp);
    }

    void set_scissor(int32_t left, int32_t top, int32_t right, int32_t bottom) override {
        D3D12_RECT rect{left, top, right, bottom};
        m_list->RSSetScissorRects(1, &rect);
    }

    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t startVertex, uint32_t startInstance) override {
        m_list->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
    }

    void draw_indexed(uint32_t indexCount, uint32_t instanceCount,
                      uint32_t startIndex, int32_t baseVertex, uint32_t startInstance) override {
        m_list->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
    }

    void draw_indirect(Buffer* args, uint32_t offset, uint32_t drawCount, uint32_t stride) override {
        auto* buf = static_cast<BufferD3D12*>(args);
        m_list->ExecuteIndirect(/* ... */);
    }
};

// Device creates command lists
std::unique_ptr<GraphicsCommandList> DeviceD3D12::create_graphics_command_list() {
    auto cmd = std::make_unique<GraphicsCommandListD3D12>();
    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                      IID_PPV_ARGS(&cmd->m_allocator));
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 cmd->m_allocator.Get(), nullptr,
                                 IID_PPV_ARGS(&cmd->m_list));
    cmd->m_list->Close();
    return cmd;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/Aether.RHI/D3D12/command_list_d3d12.cpp
git commit -m "feat: implement D3D12 command lists"
```

### Task 9: D3D12 PipelineState + SwapChain

**Files:**
- Create: `src/Aether.RHI/D3D12/pipeline_d3d12.cpp`
- Create: `src/Aether.RHI/D3D12/swapchain_d3d12.cpp`

- [ ] **Step 1: Write PipelineD3D12 + SwapChainD3D12**

`src/Aether.RHI/D3D12/pipeline_d3d12.cpp`:
```cpp
module aether.rhi;

GraphicsPipelinePtr DeviceD3D12::create_graphics_pipeline(const GfxPipelineDesc& desc) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

    if (!desc.vsBytecode.empty()) {
        psoDesc.VS = { desc.vsBytecode.data(), desc.vsBytecode.size() };
    }
    if (!desc.msBytecode.empty()) {
        psoDesc.MS = { desc.msBytecode.data(), desc.msBytecode.size() };
    }
    if (!desc.psBytecode.empty()) {
        psoDesc.PS = { desc.psBytecode.data(), desc.psBytecode.size() };
    }

    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.NumRenderTargets = 1;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    // Default rasterizer, blend, depth-stencil
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // Input layout (from VS reflection or manual)

    auto pipeline = std::make_shared<...>();
    ComPtr<ID3D12PipelineState> pso;
    m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    return pipeline;
}
```

`src/Aether.RHI/D3D12/swapchain_d3d12.cpp`:
```cpp
module aether.rhi;

struct SwapChainD3D12 : public SwapChain {
    ComPtr<IDXGISwapChain4> m_swapChain;
    std::vector<TexturePtr> m_backBuffers;
    uint32_t m_bufferCount = 3;

    uint32_t get_current_index() const override {
        return m_swapChain->GetCurrentBackBufferIndex();
    }

    TexturePtr get_back_buffer(uint32_t index) override {
        return m_backBuffers[index];
    }

    void present(bool vsync) override {
        m_swapChain->Present(vsync ? 1 : 0, 0);
    }

    void resize(uint32_t width, uint32_t height) override {
        m_backBuffers.clear();
        // Resize buffers
    }
};

SwapChainPtr DeviceD3D12::create_swap_chain(const SwapChainDesc& desc) {
    auto sc = std::make_shared<SwapChainD3D12>();
    sc->m_bufferCount = desc.bufferCount;

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = desc.width;
    scDesc.Height = desc.height;
    scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.BufferCount = desc.bufferCount;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> tempSwapChain;
    m_factory->CreateSwapChainForHwnd(
        m_directQueue.Get(),
        static_cast<HWND>(desc.windowHandle),
        &scDesc, nullptr, nullptr, &tempSwapChain);
    tempSwapChain.As(&sc->m_swapChain);

    // Create RTV descriptor heap and back buffer textures
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = desc.bufferCount;
    // ...

    return sc;
}
```

- [ ] **Step 2: Build and verify RHI compiles**

```bash
cd build
cmake --build . --target Aether.RHI
```

- [ ] **Step 3: Commit**

```bash
git add src/Aether.RHI/D3D12/*.cpp
git commit -m "feat: implement D3D12 pipeline state and swap chain"
```

### Task 10: Triangle example (D3D12)

**Files:**
- Create: `examples/triangle/CMakeLists.txt`
- Create: `examples/triangle/main.cpp`

- [ ] **Step 1: Write example CMake**

```cmake
add_executable(TriangleExample)
target_sources(TriangleExample PRIVATE main.cpp)
target_link_libraries(TriangleExample PRIVATE Aether.RHI Aether.Core)
```

- [ ] **Step 2: Write triangle main**

```cpp
import aether.rhi;
import aether.core;

struct Vertex {
    float position[4]; // float4
    float color[4];    // float4
};

int main() {
    aether::log::info("Creating D3D12 device...");

    auto device = aether::rhi::create_d3d12_device();

    // Create vertex buffer
    Vertex vertices[] = {
        {{ 0.0f,  0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    auto uploadBuffer = device->create_buffer({
        .size = sizeof(vertices),
        .heap = aether::rhi::HeapType::Upload,
        .bindFlags = aether::rhi::BindFlags::VertexBuffer,
    });
    memcpy(uploadBuffer->map(), vertices, sizeof(vertices));
    uploadBuffer->unmap();

    // Simple vertex shader bytecode (pre-compiled)
    // Create pipeline
    // Create swap chain
    // Main loop with command list recording

    aether::log::info("Engine initialized successfully!");
    return 0;
}
```

- [ ] **Step 3: Build and run**

```bash
cd build
cmake ..
cmake --build . --target TriangleExample
./examples/triangle/Debug/TriangleExample.exe
```

- [ ] **Step 4: Commit**

```bash
git add examples/triangle/
git commit -m "feat: add D3D12 triangle example"
```

---

## Phase 3: Aether.Resources (Coroutine Async Loading)

### Task 11: Resources module + ResourceLoader

**Files:**
- Create: `src/Aether.Resources/CMakeLists.txt`
- Create: `src/Aether.Resources/src/aether.resources.cppm`
- Create: `src/Aether.Resources/src/resource_loader.cpp`

- [ ] **Step 1: Write Resources CMakeLists.txt**

```cmake
add_library(Aether.Resources STATIC)
target_sources(Aether.Resources
    PUBLIC FILE_SET CXX_MODULES FILES src/aether.resources.cppm
    PRIVATE src/resource_loader.cpp
)
target_link_libraries(Aether.Resources PUBLIC Aether.RHI Aether.Core)
```

- [ ] **Step 2: Write resource module interface + loader**

The module exports `ResourceLoader` with `LoadMeshAsync`, `LoadTextureAsync`, `LoadMaterialAsync`, each returning `core::Task<T>`. Internally loads in background threads and uploads to GPU via CopyQueue.

- [ ] **Step 3: Build + commit**

---

## Phase 4: Aether.Renderer (GPU Driven Pipeline)

### Task 12: Renderer module + Scene data structures

**Files:**
- Create: `src/Aether.Renderer/CMakeLists.txt`
- Create: `src/Aether.Renderer/src/aether.renderer.cppm`
- Create: `src/Aether.Renderer/src/scene.cpp`
- Create: `src/Aether.Renderer/src/gpu_scene.cpp`

- [ ] **Step 1: Write Renderer CMakeLists.txt**

```cmake
add_library(Aether.Renderer STATIC)
target_sources(Aether.Renderer
    PUBLIC FILE_SET CXX_MODULES FILES src/aether.renderer.cppm
    PRIVATE src/scene.cpp src/gpu_scene.cpp src/gpu_culling.cpp src/indirect_draw.cpp src/lod_manager.cpp
)
target_link_libraries(Aether.Renderer PUBLIC Aether.Resources Aether.RHI Aether.Core)
```

- [ ] **Step 2: Write scene data structures + GPU buffers**

See design doc for `SceneBuffer`, `MeshBuffer`, `VisibleBuffer`, `IndirectBuffer`, `LODBuffer`. These are persistent GPU buffers containing object transforms, bounding spheres, mesh indices, LOD distances.

- [ ] **Step 3: Build + commit**

### Task 13: GPU Culling compute shader

**Files:**
- Create: `src/Aether.Renderer/shaders/culling.hlsl`
- Create: `src/Aether.Renderer/src/gpu_culling.cpp`

- [ ] **Step 1: Write culling compute shader**

A compute shader (`culling.hlsl`) that reads `SceneObjects` and writes visibility + LOD to `VisibleBuffer`. Implements frustum culling and LOD selection.

- [ ] **Step 2: Implement CullingJob dispatch**

CPU-side code that sets up descriptors, dispatches the culling compute shader, and reads the output for indirect rendering.

- [ ] **Step 3: Build + commit**

### Task 14: Indirect rendering pipeline

**Files:**
- Create: `src/Aether.Renderer/src/indirect_draw.cpp`
- Create: `src/Aether.Renderer/shaders/compact.hlsl`

- [ ] **Step 1: Write compact shader + indirect draw CPU code**

The compact shader reads `VisibleBuffer` and writes packed `DrawIndexedInstancedIndirect` args to `IndirectBuffer`. CPU then calls `draw_indirect()`.

- [ ] **Step 2: Build + commit**

### Task 15: Dynamic LOD

**Files:**
- Create: `src/Aether.Renderer/src/lod_manager.cpp`

- [ ] **Step 1: Implement LOD selection + transition**

LOD distance thresholds per object. LOD selection done in the culling shader. Dither crossfade for LOD transitions. GPU-side updates to `LODBuffer`.

- [ ] **Step 2: Build + commit**

---

## Phase 5: Aether.Shaders (Slang Integration)

### Task 16: Shaders module + Slang compiler wrapper

**Files:**
- Create: `src/Aether.Shaders/CMakeLists.txt`
- Create: `src/Aether.Shaders/src/aether.shaders.cppm`
- Create: `src/Aether.Shaders/src/shader_compiler.cpp`
- Create: `src/Aether.Shaders/src/shader_library.cpp`

- [ ] **Step 1: Write Shaders CMakeLists.txt**

```cmake
add_library(Aether.Shaders STATIC)
target_sources(Aether.Shaders
    PUBLIC FILE_SET CXX_MODULES FILES src/aether.shaders.cppm
    PRIVATE src/shader_compiler.cpp src/shader_library.cpp
)
target_link_libraries(Aether.Shaders PUBLIC Aether.Core)
target_include_directories(Aether.Shaders PRIVATE ${SLANG_SDK_PATH})
```

- [ ] **Step 2: Write ShaderCompiler wrapper around libslang**

Wraps `slang::IGlobalSession` and `slang::ICompileRequest`. `Compile()` returns `ShaderCompileResult` containing DXIL/SPIRV bytecode and reflection.

- [ ] **Step 3: Write ShaderLibrary cache**

Simple unordered_map cache keyed by shader name, storing compiled results.

- [ ] **Step 4: Build + commit**

### Task 17: Example with Slang shaders

**Files:**
- Create: `examples/slang_triangle/main.cpp`
- Create: `examples/slang_triangle/CMakeLists.txt`
- Create: `examples/slang_triangle/shaders/triangle.slang`

- [ ] **Step 1: Write a .slang shader that outputs DXIL**

```hlsl
// triangle.slang
struct VSOutput { float4 pos : SV_POSITION; float4 color : COLOR; };

[shader("vertex")]
VSOutput mainVS(float3 pos : POSITION, float4 color : COLOR) {
    VSOutput o;
    o.pos = float4(pos, 1.0);
    o.color = color;
    return o;
}

[shader("pixel")]
float4 mainPS(VSOutput input) : SV_TARGET {
    return input.color;
}
```

- [ ] **Step 2: Integrate Slang-compiled shaders with RHI pipeline**

Create pipeline from Slang-compiled DXIL bytecode, same flow as triangle example but with Slang.

- [ ] **Step 3: Build + run + commit**

---

## Phase 6: Full GPU Driven Pipeline Demo

### Task 18: End-to-end demo with many objects

**Files:**
- Create: `examples/gpu_driven_demo/main.cpp`
- Create: `examples/gpu_driven_demo/CMakeLists.txt`
- Create: `examples/gpu_driven_demo/shaders/`

- [ ] **Step 1: Create a scene with thousands of objects**

Procedurally generate objects with random positions, bounding spheres, and LOD levels. Upload scene data to GPU buffers.

- [ ] **Step 2: Implement full frame loop**

```
CPU: update camera → upload constant buffer
GPU: culling dispatch (frustum + LOD) → compact → indirect draw
GPU: present
```

- [ ] **Step 3: Measure and verify culling**

Log visible vs total object counts each frame. Visualize culling with color coding.

- [ ] **Step 4: Build + run + commit**
