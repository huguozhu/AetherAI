module aether.core;

import <vector>;
import <thread>;
import <functional>;
import <mutex>;
import <condition_variable>;
import <queue>;
import <fstream>;
import <coroutine>;
import <string_view>;
import <memory>;
import <utility>;
import <cstddef>;
import <string>;

namespace aether::concurrency {

template<typename T>
Task<T> TaskPromise<T>::get_return_object() {
    return Task<T>(std::coroutine_handle<TaskPromise<T>>::from_promise(*this));
}

Task<void> TaskPromise<void>::get_return_object() {
    return Task<void>(std::coroutine_handle<TaskPromise<void>>::from_promise(*this));
}

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
                    m_cv.wait(lock, [this] {
                        return m_stop || !m_tasks.empty();
                    });
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
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
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

// read_file_async - reads a file on a background thread
Task<std::vector<std::byte>> read_file_async(std::string_view path) {
    struct SharedState {
        std::vector<std::byte> data;
        std::string path;
    };
    auto state = std::make_shared<SharedState>();
    state->path = std::string(path);

    // Use a simple approach: create a promise and resume when done
    struct ReadFileAwaiter {
        std::shared_ptr<SharedState> state;

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) {
            ThreadPool::global().enqueue([this, handle]() {
                // Read file in background thread
                std::ifstream file(state->path, std::ios::binary | std::ios::ate);
                if (!file) {
                    state->data.clear();
                    handle.resume();
                    return;
                }
                auto size = file.tellg();
                file.seekg(0, std::ios::beg);
                state->data.resize(static_cast<size_t>(size));
                file.read(reinterpret_cast<char*>(state->data.data()), size);
                handle.resume();
            });
        }

        std::vector<std::byte> await_resume() {
            return std::move(state->data);
        }
    };

    co_return co_await ReadFileAwaiter{std::move(state)};
}

} // namespace aether::concurrency
