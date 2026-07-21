#pragma once

#include <iostream>
#include <queue>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <future>
#include <condition_variable>
#include <cstdint>

namespace logger{
namespace ctx{

class ThreadPool{
public:
    explicit ThreadPool(uint32_t count) {
        this->thread_count_.store(count);
        this->is_running_.store(false);
        this->is_shutdown_.store(false);
    }

    ~ThreadPool() {
        Stop();
    }

    bool Start();

    void Stop();

    template<typename F,typename...Args>
    void RunTask(F&& f, Args&&... args) {
        if(this->is_shutdown_ || !this->is_running_){
            return;
        }
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        {
            std::lock_guard<std::mutex>lk(mtx_);
            this->tasks_.emplace([task] { task(); });
        }
        this->cv_.notify_all();
    }

    template<typename F,typename...Args>
    auto RunRetTask(F&& f, Args&&... args)->std::shared_ptr<std::future<std::result_of_t<F(Args...)>>> {
        if(this->is_shutdown_ || !this->is_running_){
            return nullptr;
        }
        using return_type = std::result_of_t<F(Args...)>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        
        std::future<return_type>res = task->get_future();
        {
            std::lock_guard<std::mutex>lk(mtx_);
            this->tasks_.emplace([task]{ (*task)(); });
        }
        this->cv_.notify_all();
        return std::make_shared<std::future<std::result_of_t<F(Args...)>>>(std::move(res));
    }


private:
    void AddThread();

    using ThreadPtr = std::shared_ptr<std::thread>;
    struct ThreadInfo {
        ThreadInfo() = default;
        ~ThreadInfo();

        ThreadPtr ptr{nullptr};
    };

    using ThreadInfoPtr = std::shared_ptr<ThreadInfo>;

private:
    using Task = std::function<void()>;

    std::mutex mtx_;
    std::condition_variable cv_;

    std::queue<Task>tasks_;
    std::vector<ThreadInfoPtr>worker_threads_;

    std::atomic<uint32_t>thread_count_;
    std::atomic<bool>is_running_;
    std::atomic<bool>is_shutdown_;


};

}
}