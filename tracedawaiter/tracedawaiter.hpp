// Based on Frank Birbacher:
//  https://github.com/birbacher/isocpp-corotask/blob/main/src/traced.hpp

#ifndef INCLUDED_TRACED_AWAITER_HPP
#define INCLUDED_TRACED_AWAITER_HPP

#include <iostream>
#include <string_view>

template <typename Awaiter>
class TracedAwaiter {
  private:
    const std::string_view name;
  public:
    Awaiter wrappedAwaiter;

  private:
    template <typename FN>
    void dispatchSuspension(void*, FN fn) noexcept {
        std::cout << "                       dispatchSuspension(): return void"
                  << '\n';
        fn();
    }

    template <typename Handle, typename FN>
    Handle dispatchSuspension(Handle*, FN fn) noexcept {
        Handle h = fn();
        std::cout << "                       dispatchSuspension(): resume to "
                  << h.address() << '\n';
        return h;
    }

    template <typename FN>
    bool dispatchSuspension(bool*, FN fn) {
        const bool result = fn();
        std::cout << "                       dispatchSuspension(): resume conditionally: "
                  << result << '\n';
        return result;
    }

  public:
    template <std::size_t LEN_NAME>
    TracedAwaiter(const char (&name)[LEN_NAME], Awaiter wrapped)
        : name(name, LEN_NAME)
        , wrappedAwaiter(std::move(wrapped))
    {}

    auto await_ready() noexcept {
        auto result = wrappedAwaiter.await_ready();
        std::cout << "            TRACE \"" << name
                  << "\" await_ready, result " << result << '\n';
        return result;
    }
    auto await_suspend(auto arg) noexcept
        //-> decltype(wrappedAwaiter.await_suspend(arg))
    {
        const auto action = [&] { return wrappedAwaiter.await_suspend(arg); };
        decltype(action()) *p = nullptr;
        std::cout << "            TRACE \"" << name
                  << "\" await_suspend, on " << arg.address()
                  << ", " /*...details later*/
                  << '\n';
        return dispatchSuspension(p, action);
    }
    auto await_resume() noexcept
        //-> decltype(wrappedAwaiter.await_resume())
    {
        std::cout << "            TRACE \"" << name
                  << "\" await_resume\n";
        return wrappedAwaiter.await_resume();
    }
};

#endif
