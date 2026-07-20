#include "thread_pool.h"

#include <iostream>

int main(){

    logger::ctx::ThreadPool pool(5);
    pool.Start();
    std::vector<std::future<int>>result;

    auto func = [](int a, int b) {
        return a + b;
    };
    for(int i = 0; i < 5; i++) {
        auto ptr = pool.RunRetTask(func, i, i * i);
        result.emplace_back(std::move(*ptr));
    }

    for(auto& it : result) {
        std::cout << it.get() << std::endl;
    }
    return 0;
}