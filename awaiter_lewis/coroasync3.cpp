// Code for blog post 'Understanding Awaitables'
// https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await
//
// Copyright (c) Lewis Baker

#include <iostream>
#include <coroutine>
#include <exception>  // for \T{terminate()}
#include <atomic>

class async_manual_reset_event
{
public:

  async_manual_reset_event(bool initiallySet = false) noexcept;

  // No copying/moving
  async_manual_reset_event(const async_manual_reset_event&) = delete;
  async_manual_reset_event(async_manual_reset_event&&) = delete;
  async_manual_reset_event& operator=(const async_manual_reset_event&) = delete;
  async_manual_reset_event& operator=(async_manual_reset_event&&) = delete;

  bool is_set() const noexcept;

  struct awaiter;
  awaiter operator co_await() const noexcept;

  void set() noexcept;
  void reset() noexcept;

private:

  friend struct awaiter;

  // - 'this' => set state
  // - otherwise => not set, head of linked list of awaiter*.
  mutable std::atomic<void*> m_state;

};

struct async_manual_reset_event::awaiter
{
  awaiter(const async_manual_reset_event& event) noexcept
  : m_event(event)
  {}

  bool await_ready() const noexcept;
  bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept;
  void await_resume() noexcept {
    std::cout << "await_resume()\n";
  }

private:
  friend struct async_manual_reset_event;

  const async_manual_reset_event& m_event;
  std::coroutine_handle<> m_awaitingCoroutine;
  awaiter* m_next;
};

bool async_manual_reset_event::awaiter::await_ready() const noexcept
{
  bool isSet = m_event.is_set();
  std::cout << "await_ready() => " << (isSet ? "true" : "false") << '\n';
  return isSet;
}

bool async_manual_reset_event::awaiter::await_suspend(
  std::coroutine_handle<> awaitingCoroutine) noexcept
{
  std::cout << "await_suspend()\n";

  // Special m_state value that indicates the event is in the 'set' state.
  const void* const setState = &m_event;

  // Stash the handle of the awaiting coroutine.
  m_awaitingCoroutine = awaitingCoroutine;

  // Try to atomically push this awaiter onto the front of the list.
  void* oldValue = m_event.m_state.load(std::memory_order_acquire);
  do
  {
    // Resume immediately if already in 'set' state.
    if (oldValue == setState) {
      std::cout << "  event set => immediately resume\n";
      std::cout << "  => false\n";
      return false; 
    }

    // Update linked list to point at current head.
    m_next = static_cast<awaiter*>(oldValue);

    // Finally, try to swap the old list head, inserting this awaiter
    // as the new list head.
  } while (!m_event.m_state.compare_exchange_weak(
             oldValue,
             this,
             std::memory_order_release,
             std::memory_order_acquire));

  // Successfully enqueued. Remain suspended.
  std::cout << "  succsessfully queued => remain suspended\n";
  std::cout << "  => true\n";
  return true;
}

async_manual_reset_event::async_manual_reset_event(
  bool initiallySet) noexcept
: m_state(initiallySet ? this : nullptr)
{}

bool async_manual_reset_event::is_set() const noexcept
{
  return m_state.load(std::memory_order_acquire) == this;
}

void async_manual_reset_event::reset() noexcept
{
  void* oldValue = this;
  m_state.compare_exchange_strong(oldValue, nullptr, std::memory_order_acquire);
}

void async_manual_reset_event::set() noexcept
{
  // Needs to be 'release' so that subsequent 'co_await' has
  // visibility of our prior writes.
  // Needs to be 'acquire' so that we have visibility of prior
  // writes by awaiting coroutines.
  void* oldValue = m_state.exchange(this, std::memory_order_acq_rel);
  if (oldValue != this)
  {
    // Wasn't already in 'set' state.
    // Treat old value as head of a linked-list of waiters
    // which we have now acquired and need to resume.
    auto* waiters = static_cast<awaiter*>(oldValue);
    while (waiters != nullptr)
    {
      // Read m_next before resuming the coroutine as resuming
      // the coroutine will likely destroy the awaiter object.
      auto* next = waiters->m_next;
      std::cout << "\n    set(): => resume()" << std::endl;
      waiters->m_awaitingCoroutine.resume();  // BLOCKS !!
      std::cout << "\n    set():    resume() done" << std::endl;
      waiters = next;
    }
  }
}

async_manual_reset_event::awaiter
async_manual_reset_event::operator co_await() const noexcept
{
  return awaiter{ *this };
}


class CoroTask {
 public:
  // initialize members for state and customization:
  struct promise_type;
  using CoroHdl = std::coroutine_handle<promise_type>;
 private:
  CoroHdl hdl;                       // native coroutine handle
 public:
  struct promise_type {
    // the usual members:
    auto get_return_object() { return CoroHdl::from_promise(*this); }
    auto initial_suspend() { return std::suspend_always{}; }
    void return_void() { }
    void unhandled_exception() { std::terminate(); }
    auto final_suspend() noexcept { return std::suspend_always{}; }
  };

  // constructors and destructor:
  CoroTask(auto h) : hdl{h} { }
  ~CoroTask() { if (hdl) hdl.destroy(); }

  // no copying or moving:
  CoroTask(const CoroTask&) = delete;
  CoroTask& operator=(const CoroTask&) = delete;

  // API:
  // - resume the coroutine:
  bool resume() const {
    if (!hdl || hdl.done()) {
      return false;    // nothing (more) to process
    }
    hdl.resume();      // RESUME
    return !hdl.done();
  }
};

CoroTask example(async_manual_reset_event& event)
{
  std::cout << "start example()\n";
  co_await event;
  std::cout << "continue example()\n";
}

//int main()
//{
//  async_manual_reset_event ev;
//  example(ev);
//}

#include <thread>
#include <chrono>
using namespace std::literals;

int value = 0;
async_manual_reset_event event;

CoroTask consumer()
{
  std::cout << "before co_await: " << value << '\n';

  // Wait until the event is signalled by call to event.set()
  // in the producer() function.
  co_await event;

  // Now it's safe to consume 'value'
  // This is guaranteed to 'happen after' assignment to 'value'
  std::cout << "after co_await: " << value << '\n';
}

int main()
{

  std::jthread tProv{[&] {
                       std::cout << "          prov: process\n";
                       // long running computation:
                       std::this_thread::sleep_for(1s);
                       std::cout << "          prov: set value\n";
                       value = 42;

                       // Publish the value by setting the event.
                       std::cout << "          prov: set event\n";
                       event.set();
                     }};

  CoroTask cons = consumer();
  cons.resume();

  tProv.join();
}

