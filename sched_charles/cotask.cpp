// based on https://gitlab.com/charlest_uk/scheduler_demo/-/blob/main/CoTask.cpp
#include <iostream>
#include <coroutine>
#include <map>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;


// Just simple co-routine based scheduler demo code - feel free to mod and use.
// Charles Tolman ct@acm.org charlestolman.com

// main does simple ticking of globalTime.
int globalTime{0};

// CoTask configuration control structure
struct CoTaskInfo
{
  int _priority{};
  std::string _name;
  int _numRuns{};
  int _runCount{};
  int _waitCount{};

  void announce() const
  {
    std::cout
      << "    " << _name << ":start:" << _numRuns
      << " Run:" << _runCount << " Wait:" << _waitCount
      << '\n';
  }
};

// The co routine task itself initialised from CoTaskInfo
class CoTask
{
public:
  struct promise_type
  {
    CoTaskInfo _info;
    int _yieldValue{-1};

    promise_type(CoTaskInfo const & info) : _info{info} {}

    CoTask get_return_object() noexcept {
      return CoTask{Handle::from_promise(*this)};
    }

    auto yield_value(int value) {
      _yieldValue = value;
      return std::suspend_always{};
    }
    void return_void() noexcept {}
  
    auto initial_suspend() const noexcept { return std::suspend_always{}; }
    auto final_suspend() const noexcept { return std::suspend_always{}; }
    void unhandled_exception() noexcept {
      std::cerr << "CoTask: Unhandled exception caught...\n";
      std::terminate();
    }
  };

  using Handle = std::coroutine_handle<promise_type>;
  Handle _handle;

  explicit CoTask(Handle h) : _handle{h} { }
  ~CoTask() { if (_handle) _handle.destroy(); }
  CoTask(const CoTask&) = delete;
  CoTask& operator=(const CoTask&) = delete;
  
  bool resume() {
    if (!_handle || _handle.done()) {
      return false;
    }
    _handle.resume();
    return !_handle.done();
  }

  int getYieldValue() const {
    if (_handle) {
      int const value {_handle.promise()._yieldValue};
      _handle.promise()._yieldValue = -1;
      return value;
    }
    return -1;
  }

  int getPriority() const {
    if (_handle) {
      return _handle.promise()._info._priority;
    }
    return 0;
  }

  std::string getName() const {
    if (_handle) {
      return _handle.promise()._info._name;
    }
    return "";
  }
};


// Each co-routine runs <info._numRuns> loops of <info._runCount> ticks.
// Then it will wait using a yield until globalTime + <info._waitCount> ticks

CoTask coRun(CoTaskInfo const & info)
{
  info.announce();

  for (int runNum {0}; runNum < info._numRuns; ++runNum)
  {
    for (int i {0}; i < info._runCount; ++i)
    {
      std::cout << "    " << info._name << ':' << i << '\n';
      co_await std::suspend_always();
    }

    const int waitUntil{globalTime + info._waitCount};
    co_yield waitUntil;
  }

  std::cout << "    " << info._name << ":end\n";
}


// runnable task pointers ordered by priority
// could be a set since task has priority
// priority 0 is highest!
std::map<int,CoTask*> runnableTasks;

// waiting task pointers ordered by reschedule time
std::map<int,CoTask*> waitingTasks;


int main()
{
  std::cout << "START:\n";
  
  // task1: 2 runs of running for 8 ticks and waiting for 3 ticks
  CoTask task1 = coRun(CoTaskInfo{0, "task1", 2, 8, 3});
  // task2: 3 runs of running for 2 ticks and waiting for 2 ticks
  CoTask task2 = coRun(CoTaskInfo{1, "task2", 4, 2, 4});

  // put the task pointers on the runnable queue in priority order
  runnableTasks[task1.getPriority()] = &task1;
  runnableTasks[task2.getPriority()] = &task2;

  std::cout << "INIT DONE\n";

  // just run the "system" for 50 seconds
  while (globalTime < 50)
  {
    std::cout << "TIME:" << globalTime << '\n';

    // see if there are any waiting tasks that are now runnable
    auto waitIt {waitingTasks.begin()};
    while (waitIt != waitingTasks.end() && globalTime >= waitIt->first)
    {
      // we have a waiting task that is runnable 
      // move it from waiting queue to runnable queue
      CoTask * task {waitIt->second};
      waitingTasks.erase(waitIt);
      runnableTasks[task->getPriority()] = task;

      std::cout << "    " << task->getName() << " RUNNING\n";

      waitIt = waitingTasks.begin();
    }

    // Run the highest priority runnable task if any
    auto runIt {runnableTasks.begin()};
    if (runIt != runnableTasks.end())
    {
      CoTask * const task {runIt->second};
      runnableTasks.erase(runIt);

      if (task->resume())
      {
        // waitUntil will be -1 if the co task does NOT want to wait
        const int waitUntil{task->getYieldValue()};
        if (waitUntil > globalTime)
        {
          // task wants to wait and wait has not already expired
          std::cout << "    " << task->getName() << " WAITING UNTIL:" << waitUntil << '\n';
          waitingTasks[waitUntil] = task;
        }
        else
        {
          // task is still runnable
          runnableTasks[task->getPriority()] = task;
        }
      }
    }

    // this sleep is just here to give a nice controlled timing
    // of debug output. It could be done by a hardware timer perhaps.
    std::this_thread::sleep_for(1s);
    ++globalTime;
  }  
  std::cout << "END.\n";
}
