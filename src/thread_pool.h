#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <tuple>
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
    std::lock_guard<std::mutex> lock(*mtx);
    cond_var->wait(lock, [&q=*rx]() { return not q.empty(); });
    assert(not rx->empty());
    auto value = std::move(rx->front());
    rx->pop();
    return value;
  }

private:
  Receiver(
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

  std::shared_ptr<std::queue<T>> rx;
  std::shared_ptr<std::mutex> mtx;
  std::shared_ptr<std::condition_variable_any> cond_var;

  friend std::tuple<Producer<T>, Receiver<T>> channel();
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

  std::shared_ptr<std::queue<T>> tx;
  std::shared_ptr<std::mutex> mtx;
  std::shared_ptr<std::condition_variable_any> cond_var;

  friend std::tuple<Producer<T>, Receiver<T>> channel();
};

template<typename T>
std::tuple<Producer<T>, Receiver<T>>
channel()
{
  auto mtx = std::make_shared<std::mutex>();
  auto cond_var = std::make_shared<std::condition_variable_any>();
  auto q = std::make_shared<std::queue>();

  Producer tx(q, mtx, cond_var);
  Receiver rx(std::move(q), std::move(mtx), std::move(cond_var));

  return std::make_tuple(std::move(tx), std::move(rx));
}

class Worker {
public:
  // The ctor will initialize the thread and pass itself as the functor of the thread.
  Worker(Receiver<std::move_only_function<void()> tx)
    : handle(),
      rx(std::move(rx))
  {
    handle = std::jthread(*this);
  }

  // Default move ctor.
  Worker(Worker&& worker) = default;

  // Disallow copy contruction or assignment of any kind.
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;

private:
  void
  operator()() const noexcept
  {
    return;
  }

  std::jthread handle;
  mutable Receiver<std::move_only_function<void()> rx;
};

class ThreadPool {
public:
private:
  ThreadPool(
      unsigned nworkers,
      Producer<std::move_only_function<void()> producer,
      Receiver<std::move_only_function<void()> rx)
    : workers(),
      tx(std::move(producer))
  {
    for (unsigned i = 0; i < nworkers-1; ++i)
      workers.push_back(Worker(rx));

    // Move the Receiver on the last worker to avoid unnecessary copy.
    workers.push_back(Worker(std::move(rx)));
  }

  std::vector<Worker> workers;
  Producer<std::move_only_function<void()> tx;

  friend PoolOpt;
};

class PoolOpt {
public:
  static ThreadPool
  with(unsigned nthreads)
  {
    if (not nthreads)
      throw std::invalid_argument("The number of threads should be non-zero.");

    auto [tx, rx] = channel();
    return ThreadPool(nthreads, std::move(tx), std::move(rx));
  }
};

} // namespace par
