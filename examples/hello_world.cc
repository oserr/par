// Parallel hello world exmaple.

#include <chrono>
#include <cstddef>
#include <iostream>
#include <syncstream>
#include <thread>

#include "thread_pool.h"

using namespace std::literals;

void
hello_world()
{
  auto id = std::this_thread::get_id();

  {
    std::osyncstream osync(std::cout);
    osync << "hello world from thread "
          << id
          << " before going to sleep\n";
    }

  std::this_thread::sleep_for(50ms);

  {
    std::osyncstream osync(std::cout);
    osync << "hello world from thread "
          << id
          << " after going to sleep\n";
  }
}

int
main()
{
  auto tp = par::ThreadPool::with_nthreads(8);

  for (unsigned i = 0; i < 8; ++i)
    tp.submit(hello_world);

  return EXIT_SUCCESS;
}
