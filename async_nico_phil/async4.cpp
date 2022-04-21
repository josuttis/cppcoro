// async coro exmaple
//  by Nico Josuttis and Phil Nash
//  @ACCU 5.4.22
// basic ideas come from:
//  https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer
// tracedawaiter comes from Frank Birbacher

#include <iostream>
#include <coroutine>
#include <exception>   // for std::terminate()
#include <utility>     // for std::exchange()
#include <cassert>
//#include "../tracedawaiter/tracedawaiter.hpp"

#include <syncstream>
auto coutSync() {
  return std::osyncstream{std::cout};
}


class CoroTask {
 public:
  // initialize members for state and customization:
  struct promise_type;
  using CoroHdl = std::coroutine_handle<promise_type>;
 private:
  CoroHdl hdl;  //hdl;            // native coroutine handle
 public:
  struct promise_type {
    std::string name = "CoroTask::promise_type ?????";
 
    auto get_return_object() noexcept {
      coutSync() << "CoroTaskPromise: get_return_object()\n";
      return CoroTask{CoroHdl::from_promise(*this)};  // CoroTask{...} necessary here
    }
    auto initial_suspend() {
      coutSync() << "CoroTaskPromise: initial_suspend() for " << name << '\n';
      return std::suspend_always{};
    }
    void unhandled_exception() noexcept { std::terminate(); }

    void return_void() noexcept { }
    //std::string coroValue;
    //void return_value(const std::string& s) {
    //  coutSync() << "CoroTaskPromise: return_value(" << s << ") for " << name << '\n';
    //  coroValue = s;
    //}

    // Finally, when the coroutine execution reaches the closing curly brace,
    // we want the coroutine to suspend at the final-suspend point
    // (and then resume its continuation. I.e., the coroutine that is
    //  awaiting the completion of this coroutine).
    // It’s important to note that the coroutine is not yet in a suspended state
    // when the final_suspend() method is invoked. We need to wait until
    // the await_suspend() method on the returned awaitable is called
    // before the coroutine is suspended.
    // Therefore NOT:
    //   auto final_suspend() noexcept { return std::suspend_always{}; }
    // But:
    std::coroutine_handle<> hdl;
    struct FinalAwaiter {
      bool await_ready() noexcept {
        return false;
      }
      void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
        coutSync() << "CoroTaskPromise: await_suspend() for " << h.promise().name << '\n';
        // The coroutine is now suspended at the final-suspend point.
        // Lookup its passed final handle in the promise and resume it.
        h.promise().hdl.resume();
      }
      void await_resume() noexcept {}
    };
    auto final_suspend() noexcept {
      return FinalAwaiter{};
    }
  };

  CoroTask(CoroTask&& t) noexcept
  : hdl(std::exchange(t.hdl, {}))
  {}

  ~CoroTask() {
    if (hdl)
      hdl.destroy();
  }

  // When evaluating a co_await expression,
  // the compiler will generate a call to operator co_await().
  // For co_await task
  // we want the awaiting coroutine to always suspend
  // and then, once it has suspended,
  //  store the awaiting coroutine’s handle in the promise
  //  of the coroutine we are about to resume
  // and then call .resume() on the task’s std::coroutine_handle
  //  to start executing the task.
  class CoroTaskAwaiter {
    friend CoroTask;
   private:
    std::coroutine_handle<promise_type> waitingHdl;

    explicit CoroTaskAwaiter(std::coroutine_handle<promise_type> h) noexcept
     : waitingHdl(h) {
      std::cout << "   CoroTaskAwaiter(): store handle for "
                << h.promise().name << "\n";
    }
   public:
    bool await_ready() noexcept {
      return false;
    }

    //void await_suspend(std::coroutine_handle<> hdl) noexcept {
    void await_suspend(auto hdl) noexcept {
      std::cout << "   CoroTaskAwaiter(): await_suspend() "
                << hdl.promise().name << "\n";
      // Store the continuation in the task's promise so that the final_suspend()
      // knows to resume this coroutine when the task completes.
      waitingHdl.promise().hdl = hdl;

      // Then we resume the task's coroutine, which is currently suspended
      // at the initial-suspend-point (ie. at the open curly brace).
      std::cout << "   CoroTaskAwaiter():       resume => "
                << waitingHdl.promise().name << '\n';
      waitingHdl.resume();
    }

    void await_resume() noexcept {
    }
  };  

  auto operator co_await() && noexcept {
    std::cout << "CoroTask: op co_await() for "
              << hdl.promise().name << '\n';
    return CoroTaskAwaiter{hdl};
  }

  void setName(std::string id) {
    //std::cout << "CoroTask: setName()\n";
    hdl.promise().name = "CoroTask " + id;
  }
  auto getHandle() const {
    return hdl;
  }

private:
  explicit CoroTask(std::coroutine_handle<promise_type> h) noexcept
  : hdl{h}
  {}
};


struct SchedulerTask {
  struct promise_type {
    std::string name = "SchedulerTask::promise_type ?????";
    SchedulerTask get_return_object() noexcept {
      return SchedulerTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return{}; }
    std::suspend_always final_suspend() noexcept { return{}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  std::coroutine_handle<promise_type> coro_;

  explicit SchedulerTask(std::coroutine_handle<promise_type> h) noexcept : coro_(h) {}

  SchedulerTask(SchedulerTask&& t) noexcept : coro_(t.coro_) {
    t.coro_ = {};
  }

  ~SchedulerTask() {
    if (coro_) coro_.destroy();
  }

  static SchedulerTask start(CoroTask&& t) {
    std::cout << "SchedulerTask: start() callig co_await for "
              << t.getHandle().promise().name << "\n";
    co_await std::move(t);
  }

  bool done() {
    return coro_.done();
  }
};

struct CoroScheduler {
  struct ScheduleAwaiter {
    CoroScheduler& sched;
    ScheduleAwaiter* next_ = nullptr;
    CoroTask::CoroHdl contHdl;

    ScheduleAwaiter(CoroScheduler& executor)
     : sched(executor) {
      std::cout << "ScheduleAwaiter: constructor\n";
    }

    bool await_ready() noexcept { return false; }

    void await_suspend(auto cHdl) noexcept {
      std::cout << "ScheduleAwaiter: await_suspend() for "
                << cHdl.promise().name << '\n';
      contHdl = cHdl;
      next_ = sched.head_;
      sched.head_ = this;
    }

    void await_resume() noexcept {
      std::cout << "ScheduleAwaiter: await_resume()\n";
    }
  };

  ScheduleAwaiter* head_ = nullptr;

  ScheduleAwaiter schedule() noexcept {
    std::cout << "schedule()\n";
    return ScheduleAwaiter{*this};
  }

  void drain() {
    while (head_ != nullptr) {
      auto* item = head_;
      head_ = item->next_;
      assert(head_ == nullptr);
      std::cout << "ScheduleSchedule: >>> resume() " << item->contHdl.promise().name << "\n";
      item->contHdl.resume();
      std::cout << "ScheduleSchedule: <<< resume() DONE\n";
    }
  }

  void add(CoroTask&& t) {
    auto t2 = SchedulerTask::start(std::move(t));
    while (!t2.done()) {
      drain();
    }
  }
};


/*
CoroTask foo() {
  coutSync() << "foo(): does something\n";
  co_return;
}

CoroTask bar() {
  coutSync() << "bar(): call foo()\n";
  co_await foo();
  coutSync() << "bar(): done\n";
}

int main()
{
  foo();
}
*/

//*************************************************
// user code
//*************************************************

CoroTask foo(CoroScheduler& sched)
{
  std::cout << "****** inside foo()\n";
  // Suspend coroutine and reschedule onto thread-pool thread.
  co_await sched.schedule();
  std::cout << "****** about to return from foo()\n";
  co_return;
}

CoroTask callFoo(CoroScheduler& sched)
{
  std::cout << "*** inside callFoo()\n";
  std::cout << "***   about to call foo()\n";
  //co_await foo(sched);
  auto&& coro = foo(sched);
  coro.setName("foo() FIRST");
  co_await std::move(coro);
  std::cout << "***   done calling foo() call foo() AGAIN\n";
  auto&& coro2 = foo(sched);
  coro2.setName("foo() SECOND");
  co_await std::move(coro2);
  std::cout << "***   done calling foo()\n";
}

int main()
{
  CoroScheduler sched;
  auto coro = callFoo(sched);
  coro.setName("callFoo()");
  sched.add(std::move(coro));
}

