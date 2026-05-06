module;
#include <cmath>
#include <string_view>
#include <format>
#include <coroutine>
#include <optional>
#include <exception>
#include <concepts>
#include <vector>
#include <thread>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <iostream>
#include <queue>
#include <fstream>

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
    float4 planes[6]; // left, right, top, bottom, near, far
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

void* allocate_aligned(size_t size, size_t alignment = 256);
void  free_aligned(void* ptr);

template<typename T>
class DynamicBuffer {
public:
    DynamicBuffer() = default;

    void append(const T& item) { m_data.push_back(item); }

    T* data() noexcept { return m_data.data(); }
    const T* data() const noexcept { return m_data.data(); }
    size_t size() const noexcept { return m_data.size(); }
    bool empty() const noexcept { return m_data.empty(); }

    void clear() { m_data.clear(); }
    void reserve(size_t capacity) { m_data.reserve(capacity); }

    T& operator[](size_t i) { return m_data[i]; }
    const T& operator[](size_t i) const { return m_data[i]; }

    T* begin() noexcept { return m_data.data(); }
    T* end() noexcept { return m_data.data() + m_data.size(); }
    const T* begin() const noexcept { return m_data.data(); }
    const T* end() const noexcept { return m_data.data() + m_data.size(); }

private:
    std::vector<T> m_data;
};

} // namespace aether::memory

// === Coroutine Task + ThreadPool (forward declarations) ===
export namespace aether::concurrency {

class ThreadPool;

template<typename T>
class Task;

template<typename T>
struct TaskPromise {
    std::coroutine_handle<> m_continuation;
    std::exception_ptr m_exception;
    std::optional<T> m_result;

    Task<T> get_return_object();
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { m_exception = std::current_exception(); }
    template<typename U>
    requires std::convertible_to<U&&, T>
    void return_value(U&& value) { m_result = std::forward<U>(value); }
};

template<>
struct TaskPromise<void> {
    std::coroutine_handle<> m_continuation;
    std::exception_ptr m_exception;

    Task<void> get_return_object();
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void unhandled_exception() { m_exception = std::current_exception(); }
    void return_void() {}
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

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        m_handle.promise().m_continuation = awaiting;
        return m_handle;
    }

    T await_resume() {
        if (m_handle.promise().m_exception) {
            std::rethrow_exception(m_handle.promise().m_exception);
        }
        return std::move(*m_handle.promise().m_result);
    }

private:
    std::coroutine_handle<promise_type> m_handle;
};

template<>
class [[nodiscard]] Task<void> {
public:
    using promise_type = TaskPromise<void>;

    Task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    ~Task() { if (m_handle) m_handle.destroy(); }

    Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    Task& operator=(Task&&) = delete;
    Task(const Task&) = delete;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        m_handle.promise().m_continuation = awaiting;
        return m_handle;
    }

    void await_resume() {
        if (m_handle.promise().m_exception) {
            std::rethrow_exception(m_handle.promise().m_exception);
        }
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

// Utility: read file async
export Task<std::vector<std::byte>> read_file_async(std::string_view path);

} // namespace aether::concurrency
