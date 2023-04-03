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

template <typename T, size_t Size>
class UninitializedArray {
 public:
  template <typename... Ts>
  void Emplace(size_t index, Ts... ts) {
    ::new (mut(index)) T(std::forward<Ts>(ts)...);
  }
  T& operator[](size_t index) { return *mut(index); }
  const T& operator[](size_t index) const { return *ref(index); }

  T* data() { return mut(0); }
  const T* data() const { return ref(0); }

  T* begin() { return data(); }
  T* end() { return data() + Size; }

  const T* cbegin() const { return data(); }
  const T* cend() const { return data() + Size; }

 private:
  T* mut(size_t i) { return reinterpret_cast<T*>(&buffer_[i * sizeof(T)]); }
  const T* ref(size_t i) const {
    return reinterpret_cast<const T*>(&buffer_[i * sizeof(T)]);
  }

  alignas(T) uint8_t buffer_[Size * sizeof(T)];
};

}  // namespace G

#endif