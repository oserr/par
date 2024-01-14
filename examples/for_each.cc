#include <chrono>
#include <iostream>
#include <numeric>
#include <ranges>
#include <syncstream>
#include <thread>

#include "par.h"

int
add_values(const std::vector<int>& coll)
{
  return std::reduce(coll.begin(), coll.end());
}

int
sum_n(std::uint64_t n)
{
  std::uint64_t total = 0;
  for (std::uint64_t i = 0; i < n; ++i)
    total += i;
  return total;
}

void
print_value(std::int64_t value)
{
  std::osyncstream osync(std::cout);
  osync << "value=" << value << std::endl;
}

int
main()
{
  std::vector<std::vector<int>> colls;
  for (int i = 0; i < 10; ++i)
    colls.emplace_back(100, 1);

  auto tp = par::WorkQ::with_nthreads(10);
  auto futs = tp.for_each(colls, add_values);

  for (int i = 0; auto& fut : futs) {
    std::cout << "The value of future " << i
              << " for add_values is "
              << fut.get() << std::endl;
    ++i;
  }

  futs = tp.for_range(101, sum_n);
  for (int i = 0; auto& fut : futs) {
    std::cout << "The value of future " << i
              << " for sum_n is "
              << fut.get() << std::endl;
    ++i;
  }

  (void)tp.for_range(-10, 20, print_value);

  std::this_thread::sleep_for(std::chrono::seconds(1));
}
