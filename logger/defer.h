#pragma once

#include <functional>

namespace logger{

class ExecuteOnScopeExit{
public:
    ExecuteOnScopeExit() = default;

    ExecuteOnScopeExit(ExecuteOnScopeExit&&) = default;

    ExecuteOnScopeExit& operator=(ExecuteOnScopeExit&&) = default;

    ExecuteOnScopeExit(const ExecuteOnScopeExit&) = delete;

    ExecuteOnScopeExit& operator=(const ExecuteOnScopeExit&) = delete;

    template<typename F,typename... Args>
    ExecuteOnScopeExit(F&& f,Args&&... args){
        func_ = std::bind(std::forward<F>(f),std::forward<Args>(args)...);
    }

    ~ExecuteOnScopeExit(){
        if(func_){
            func_();
        }
    }

private:
    std::function<void()>func_;
};

}

#define _LOG_CONTACT(a, b) a##b
#define _MAKE_DEFER_(line) logger::ExecuteOnScopeExit _LOG_CONTACT(defer, line) = [&]()

#define LOG_DEFER _MAKE_DEFER_(__LINE__)