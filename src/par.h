#pragma once

#include <cassert>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
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

// Forward declaration of Producer so we can name Producer in a friend
// declaration inside of Receiver, which is defined below.
template<typename T>
class Producer;

//! Receiver functions as the receving end of a thread-safe one-way communication
//! channel between one or more producers and one or more receivers. It is
//! essentially a thread safe queue, but modeled as two ends.
template<typename T>
class Receiver {
public:
  // Disallow default construction to simplify creating a pair of (Producer,
  // Receiver) in a safe manner. Otherwise, for example, we'd be able to create
  // a Producer and Receiver with different underlying queues that are not
  // connected. Note that this doesn't prevent someone from creating two pairs
  // of channels and then mixing up the producers and receivers, but making
  // construction of both pieces in one step makes misuse less likely, and make
  // it more clear that the two pieces are meant to be used together.
  Receiver() = delete;


  // Allow copy/move construction and assignment because having multiple
  // receivers makes perfect sense, for example, as used below in the thread
  // pool, where each worker thread has its own copy of the Receiver for tasks.

  Receiver(const Receiver& rx) = default;
  Receiver(Receiver&& rx) = default;

  Receiver&
  operator=(const Receiver& rx) = default;

  Receiver&
  operator=(Receiver&& rx) = default;


  //! recv blocks the current thread until it receives a message T from a
  //! producer.
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

  //! try_recv blocks the current thread until either it receives a message T
  //! from a producer, or the thread waiting for the message receives a signal
  //! that it should stop working, in which it won't return anything.
  //!
  //! @param st A stop token which can be used by a separate thread to notify
  //!  the thread waiting on try_recv that it should stop waiting, which can
  //!  override the condition variable if the condition is not met yet.
  //! @return A message T if a message is available to this thread, or null if
  //!  the thread has been stopped before a message was avaiable.
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
  //! Initializes a Receiver with the underlying structures to create a channel
  //! between the Producer and Receiver, a queue to convey messages FIFO, a
  //! mutex to protect access, and a condition variable to allow Producers and
  //! Receivers to notify each other when messages are available.
  //!
  //! @param rx A shared pointer to the queue for receving messages.
  //! @param mtx A shared pointer to a mutex to protect access to the queue.
  //! @param cond_var A shared pointer to a condition variable to receive
  //!  notifications from one or more producers.
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

  //! A function to create a channel between a producer and a receiver.
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

  template<typename... F>
  [[nodiscard]] auto
  submit_all(F... fns)
  { return std::make_tuple(submit(fns)...); }

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
