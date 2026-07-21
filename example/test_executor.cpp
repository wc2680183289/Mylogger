#include "executor.h"
#include <iostream>
#include <chrono>
#include <atomic>
#include <thread>

int main() {
  logger::ctx::Executor ex;
  auto tag = ex.AddTaskRunner(1);

  std::atomic<int> n{0};
  ex.PostTask(tag, [&] { n = 1; });

  std::atomic<bool> delayed{false};
  ex.PostDelayedTask(tag, [&] { delayed = true; }, std::chrono::milliseconds(100));

  auto fut = ex.PostTaskAndGetResult(tag, [](int a, int b) { return a + b; }, 2, 3);

  while (n.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  std::cout << "n=" << n.load() << " delayed=" << delayed.load() << " sum=" << fut->get() << std::endl;
  return 0;
}