// Parallel hello world exmaple.

#include <chrono>
#include <cstddef>
#include <iostream>
#include <syncstream>
#include <thread>

#include "par.h"

using namespace std::literals;

void
hello_world(unsigned i)
{
  auto id = std::this_thread::get_id();

  {
    std::osyncstream osync(std::cout);
    osync << "hello world " << i
          << " from thread "
          << id
          << " before going to sleep\n";
    }

  std::this_thread::sleep_for(50ms);

  {
    std::osyncstream osync(std::cout);
    osync << "hello world " << i
          << " from thread "
          << id
          << " after going to sleep\n";
  }
}

int
hello1()
{
  auto id = std::this_thread::get_id();

  {
    std::osyncstream osync(std::cout);
    osync << "hello1 from thread "
          << id << '\n';
    }

  std::this_thread::sleep_for(50ms);

  {
    std::osyncstream osync(std::cout);
    osync << "good bye from hello1\n";
  }

  return 5;
}

std::string
hello2()
{
  auto id = std::this_thread::get_id();

  {
    std::osyncstream osync(std::cout);
    osync << "hello2 from thread "
          << id << '\n';
    }

  std::this_thread::sleep_for(50ms);

  {
    std::osyncstream osync(std::cout);
    osync << "good bye from hello2\n";
  }

  return "karma";
}

float
hello3()
{
  auto id = std::this_thread::get_id();

  {
    std::osyncstream osync(std::cout);
    osync << "hello3 from thread "
          << id << '\n';
    }

  std::this_thread::sleep_for(50ms);

  {
    std::osyncstream osync(std::cout);
    osync << "good bye from hello3\n";
  }

  return 3.14;
}

int
main()
{
  auto tp = par::WorkQ::with_nthreads(8);

  for (unsigned i = 0; i < 8; ++i)
    (void)tp.submit(hello_world, i);

  std::this_thread::sleep_for(100ms);

  auto [f1, f2, f3] = tp.submit_all(hello1, hello2, hello3);

  std::osyncstream osync(std::cout);

  osync << "f1.get()=" << f1.get() << '\n'
        << "f2.get()=" << f2.get() << '\n'
        << "f3.get()=" << f3.get() << std::endl;

  return EXIT_SUCCESS;
}
