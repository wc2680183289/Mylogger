#include "executor.h"

namespace logger{
namespace ctx{

Executor::ExecutorTimer::ExecutorTimer() {
    this->is_running_.store(false);
    this->thread_pool_ = std::make_unique<ThreadPool>(1);
    this->repeated_task_id_.store(0);
}

Executor::ExecutorTimer::~ExecutorTimer() {
    Stop();
}

void Executor::ExecutorTimer::Run_() {
    while(this->is_running_){
        std::unique_lock<std::mutex>lk(mtx_);
        // while(this->queue_.empty()){
        //     cv_.wait(lk);
        // }
        if(this->queue_.empty()) {
            cv_.wait(lk);
            continue;
        }
        auto t = queue_.top();
        auto diff = t.time_point - std::chrono::high_resolution_clock::now();
        if(std::chrono::duration_cast<std::chrono::microseconds>(diff).count() > 0){
            cv_.wait_for(lk, diff);
        } else {
            queue_.pop();
            lk.unlock();
            t.task();
        }
    }
}

bool Executor::ExecutorTimer::Start() {
    if(this->is_running_){
        return false;
    }
    this->is_running_.store(true);
    bool ret = this->thread_pool_->Start();
    this->thread_pool_->RunTask(&Executor::ExecutorTimer::Run_, this);
    return ret;
}

void Executor::ExecutorTimer::Stop() {
    this->is_running_.store(false);
    cv_.notify_all();
    thread_pool_.reset();
}

void Executor::ExecutorTimer::PostDelayedTask(Task task, const std::chrono::microseconds& delta) {
    InternalS s;
    s.time_point = std::chrono::high_resolution_clock::now() + delta;
    s.task = std::move(task);
    {
        std::unique_lock<std::mutex>lk(mtx_);
        queue_.push(s);
        cv_.notify_all();
    }
}

RepeatedTaskId Executor::ExecutorTimer::PostRepeatedTask(Task task, const std::chrono::microseconds& delta, uint64_t repeat_num) {
    RepeatedTaskId id = GetNextRepeatedTaskId();
    repeated_id_state_set_.insert(id);
    PostRepeatedTask_(std::move(task), delta, id, repeat_num);
    return id;
}

void Executor::ExecutorTimer::PostTask_(Task task, std::chrono::microseconds delta, RepeatedTaskId repeated_task_id, uint64_t repeat_num) {

    PostRepeatedTask_(std::move(task), delta, repeated_task_id, repeat_num);
}

void Executor::ExecutorTimer::PostRepeatedTask_(Task task, const std::chrono::microseconds& delta,
    RepeatedTaskId repeated_task_id, uint64_t repeat_num) {

    if(repeated_id_state_set_.find(repeated_task_id) == repeated_id_state_set_.end()) {
        return;
    }

    task();
    InternalS s;
    auto func = std::bind(&Executor::ExecutorTimer::PostTask_, this, task, delta, repeated_task_id, repeat_num);
    s.task = std::move(func);
    s.repeated_id = repeated_task_id;
    s.time_point = std::chrono::high_resolution_clock::now() + delta;
    {
        std::unique_lock<std::mutex>lk(mtx_);
        queue_.push(s);
        cv_.notify_all();
    }
}

void Executor::ExecutorTimer::CancelRepeatedTask(RepeatedTaskId task_id) {
    repeated_id_state_set_.erase(task_id);
}


TaskRunnerTag Executor::ExecutorContext::GetNextRunnerTag() {
    static uint64_t idx = 0;
    ++idx;
    return idx;
}

TaskRunnerTag Executor::ExecutorContext::AddTaskRunner(const TaskRunnerTag& tag){
    std::lock_guard<std::mutex>lk(mtx_);
    TaskRunnerTag lastest_tag = tag;
    while(task_runners_.find(lastest_tag) != task_runners_.end()) {
        lastest_tag = GetNextRunnerTag();
    }
    TaskRunnerPtr runner = std::make_unique<TaskRunner>(1);
    runner->Start();
    task_runners_.emplace(lastest_tag,std::move(runner));
    return lastest_tag;
}

Executor::ExecutorContext::TaskRunner* Executor::ExecutorContext::GetTaskRunner(const TaskRunnerTag& tag) {
    std::lock_guard<std::mutex>lk(mtx_);
    if(task_runners_.find(tag) == task_runners_.end()) {
        return nullptr;
    }

    return task_runners_[tag].get();
}

Executor::Executor() {
    executor_context_ = std::make_unique<ExecutorContext>();
    executor_timer_  = std::make_unique<ExecutorTimer>();
}

Executor::~Executor() {
    executor_context_.reset();
    executor_timer_.reset();
}

TaskRunnerTag Executor::AddTaskRunner(const TaskRunnerTag& tag) {
    return executor_context_->AddTaskRunner(tag);
}

void Executor::PostTask(const TaskRunnerTag& runner_tag, Task task) {
    ExecutorContext::TaskRunner* task_runner = executor_context_->GetTaskRunner(runner_tag);
    task_runner->RunTask(std::move(task));
}

}
}