#pragma once
#ifndef _GAME_DEFER_H
#define _GAME_DEFER_H

#include <utility>

namespace G {

template <typename F>
class DeferCall {
 public:
  DeferCall(F&& f) : f_(f) {}

  ~DeferCall() { std::move(f_)(); }

 private:
  F f_;
};

template <typename F>
DeferCall<F> MakeDefer(F&& f) {
  return DeferCall(std::move(f));
}

#define DEFER_1(X, Y) X##Y
#define DEFER_2(X, Y) DEFER_1(X, Y)
#define DEFER_3(X) DEFER_2(X, __COUNTER__)
#define DEFER(code) auto DEFER_3(_defer_) = ::G::MakeDefer(code)

}  // namespace G

#endif  // _GAME_DEFER_H
