#pragma once

#include <cassert>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace par {

// Forward declaration.
template<typename T>
class Producer;

template<typename T>
class Receiver {
public:
  Receiver() = delete;

  Receiver(const Receiver& rx) = default;
  Receiver(Receiver&& rx) = default;

  Receiver&
  operator=(const Receiver& rx) = default;

  Receiver&
  operator=(Receiver&& rx) = default;

  T
  recv()
  {
    std::unique_lock<std::mutex> lock(*mtx);
    cond_var->wait(lock, [rx=rx.get()]() { return not rx->empty(); });
    assert(not rx->empty());
    auto value = std::move(rx->front());
    rx->pop();
    return value;
  }

  std::optional<T>
  try_recv(std::stop_token st)
  {
    std::unique_lock<std::mutex> lock(*mtx);
    if (not cond_var->wait(lock, std::move(st),
          [rx=rx.get()]() { return not rx->empty(); }))
      return std::nullopt;
    assert(not rx->empty());
    auto value = std::move(rx->front());
    rx->pop();
    return value;
  }

private:
  Receiver(
      std::shared_ptr<std::queue<T>> rx,
      std::shared_ptr<std::mutex> mtx,
      std::shared_ptr<std::condition_variable_any> cond_var)
    : rx(std::move(rx)),
      mtx(std::move(mtx)),
      cond_var(std::move(cond_var))
  {
    if (not this->rx)
      throw std::invalid_argument("rx is null.");

    if (not this->mtx)
      throw std::invalid_argument("mtx is null.");

    if (not this->cond_var)
      throw std::invalid_argument("mtx is null.");
  }

  mutable std::shared_ptr<std::queue<T>> rx;
  mutable std::shared_ptr<std::mutex> mtx;
  mutable std::shared_ptr<std::condition_variable_any> cond_var;

  friend std::tuple<Producer<T>, Receiver<T>> channel<T>();
};

template<typename T>
class Producer {
public:
  Producer() = delete;
  Producer(const Producer& tx) = default;
  Producer(Producer&& tx) = default;

  Producer&
  operator=(const Producer& tx) = default;

  Producer&
  operator=(Producer&& tx) = default;

  void
  send(T&& value)
  {
    std::lock_guard<std::mutex> lock(*mtx);
    tx->push(std::move(value));
    cond_var->notify_all();
  }

  void
  send(const T& value)
  {
    std::lock_guard<std::mutex> lock(*mtx);
    tx->push(value);
    cond_var->notify_all();
  }

private:
  Producer(
      std::shared_ptr<std::queue<T>> tx,
      std::shared_ptr<std::mutex> mtx,
      std::shared_ptr<std::condition_variable_any> cond_var)
    : tx(std::move(tx)),
      mtx(std::move(mtx)),
      cond_var(std::move(cond_var))
  {
    if (not this->tx)
      throw std::invalid_argument("tx is null.");

    if (not this->mtx)
      throw std::invalid_argument("mtx is null.");

    if (not this->cond_var)
      throw std::invalid_argument("mtx is null.");
  }

  mutable std::shared_ptr<std::queue<T>> tx;
  mutable std::shared_ptr<std::mutex> mtx;
  mutable std::shared_ptr<std::condition_variable_any> cond_var;

  friend std::tuple<Producer<T>, Receiver<T>> channel<T>();
};

template<typename T>
std::tuple<Producer<T>, Receiver<T>>
channel()
{
  auto mtx = std::make_shared<std::mutex>();
  auto cond_var = std::make_shared<std::condition_variable_any>();
  auto q = std::make_shared<std::queue<T>>();

  Producer tx(q, mtx, cond_var);
  Receiver rx(q, mtx, cond_var);

  return std::make_tuple(std::move(tx), std::move(rx));
}

using TaskFn = std::move_only_function<void()>;

class Worker {
public:
  // The ctor will initialize the thread and pass itself as the functor of the
  // thread.
  Worker(Receiver<TaskFn> rx)
    : handle(),
      rx(std::move(rx))
  {
    handle = std::jthread(&Worker::work, this);
  }

  // Default move ctor.
  Worker(Worker&& worker) = default;

  // Disallow copy contruction or assignment of any kind.
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;

  void
  stop()
  { handle.request_stop(); }

  void
  join()
  { handle.join(); }

private:
  void
  work() const
  {
    auto token = handle.get_stop_token();
    while (not token.stop_requested()) {
      auto task = rx.try_recv(token);
      if (not task) continue;
      (*task)();
    }
  }

  std::jthread handle;
  mutable Receiver<TaskFn> rx;
};

class ThreadPool {
public:
  static ThreadPool
  with_nthreads(unsigned nthreads)
  {
    if (not nthreads)
      throw std::invalid_argument("The number of threads should be non-zero.");

    auto [tx, rx] = channel<TaskFn>();
    return ThreadPool(nthreads, std::move(tx), std::move(rx));
  }

  template<
    typename F,
    typename... Args,
    typename R = std::invoke_result_t<F, Args...>>
  [[nodiscard]] std::future<R>
  submit(F func, Args... args)
  {
    std::promise<R> prom;
    auto fut = prom.get_future();

    auto fn = [prom=std::move(prom),
               func=std::move(func),
               ...args=std::move(args)]() mutable
    {
      try {
        if constexpr (not std::is_void_v<R>) {
          prom.set_value(std::invoke(func, args...));
        } else {
          std::invoke(func, args...);
          prom.set_value();
        }
      } catch (...) {
        prom.set_exception(std::current_exception());
      }
    };

    tx.send(std::move(fn));

    return fut;
  }

  template<typename Coll, typename F>
  [[nodiscard]] auto
  for_each(const Coll& coll, F fn)
  {
    using R = std::invoke_result_t<F, decltype(*coll.begin())>;
    std::vector<std::future<R>> futs;
    futs.reserve(coll.size());

    for (auto val : coll)
      futs.emplace_back(submit(fn, std::move(val)));

    return futs;
  }

  template<typename F, typename R = std::invoke_result_t<F, std::uint64_t>>
  [[nodiscard]] std::vector<std::future<R>>
  for_range(std::uint64_t last, F fn)
  {
    std::vector<std::future<R>> futs;
    futs.reserve(last);

    for (std::uint64_t i = 0; i < last; ++i)
      futs.emplace_back(submit(fn, i));

    return futs;
  }

  template<typename F, typename R = std::invoke_result_t<F, std::int64_t>>
  [[nodiscard]] std::vector<std::future<R>>
  for_range(std::int64_t first, std::int64_t last, F fn)
  {
    if (last < first)
      throw std::invalid_argument("last should be greater than first");

    std::vector<std::future<R>> futs;
    futs.reserve(last - first);

    for (; first < last; ++first)
      futs.emplace_back(submit(fn, first));

    return futs;
  }

  // Stop all the workers.
  ~ThreadPool()
  {
    for (auto& worker : workers)
      worker.stop();

    for (auto& worker : workers)
      worker.join();
  }

private:
  ThreadPool(
      unsigned nworkers,
      Producer<TaskFn> tx,
      Receiver<TaskFn> rx)
    : workers(),
      tx(std::move(tx))
  {
    workers.reserve(nworkers);
    for (unsigned i = 0; i < nworkers; ++i)
      workers.emplace_back(rx);
  }

  std::vector<Worker> workers;
  Producer<TaskFn> tx;
};

} // namespace par
