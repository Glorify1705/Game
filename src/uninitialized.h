#pragma once
#ifndef _GAME_UNINITIALIZED_H
#define _GAME_UNINITIALIZED_H

#include <cstdint>
#include <utility>

#include "logging.h"

namespace G {

template <typename T>
struct Uninitialized {
 public:
  Uninitialized() = default;

  template <typename... Ts>
  void Init(Ts... ts) {
    ::new (mut()) T(std::forward<Ts>(ts)...);
  }

  Uninitialized(Uninitialized&& u) { *mut() = u; }

  Uninitialized& operator=(T&& u) {
    *mut() = u;
    return *this;
  }

  T* operator->() { return mut(); }
  const T* operator->() const { return ref(); }
  T& operator*() { return *mut(); }
  const T& operator*() const { return *ref(); }

 private:
  T* mut() { return reinterpret_cast<T*>(&buf_); }
  const T* ref() const { return reinterpret_cast<const T*>(&buf_); }

  char buf_[sizeof(T)];
};

}  // namespace G

#endif