#include "thread_pool.h"

namespace logger{
namespace ctx{

ThreadPool::ThreadInfo::~ThreadInfo() {
    if(ptr && ptr->joinable()) {
        ptr->join();
    }
}

void ThreadPool::AddThread() {
    ThreadInfoPtr thread_ptr = std::make_shared<ThreadInfo>();
    auto func = [this](){
        while(true){
            Task task;
            {
                std::unique_lock<std::mutex>lk(mtx_);
                while(!this->is_shutdown_ && this->tasks_.empty()) {
                    cv_.wait(lk);
                }

                if(this->is_shutdown_) {
                    break;
                }

                task = std::move(this->tasks_.front());
                this->tasks_.pop();
            }
            task();
        }
    };
    thread_ptr->ptr = std::make_shared<std::thread>(std::move(func));
    this->worker_threads_.emplace_back(std::move(thread_ptr));
}

bool ThreadPool::Start() {
    if(this->is_running_){
        return false;
    }
    this->is_running_.store(true);
    this->is_shutdown_.store(false);
    for(int i = 0; i < this->thread_count_; i++) {
        AddThread();
    }
    return true;
}

void ThreadPool::Stop() {
    if(this->is_running_) {
        this->is_shutdown_.store(true);
        cv_.notify_all();
        this->is_running_.store(false);
    }
    this->worker_threads_.clear();
}

}
}