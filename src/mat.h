// DO NOT EDIT - EDIT WITH matrices.py SCRIPT INSTEAD.
#pragma once
#ifndef _GAME_MATRICES_H
#define _GAME_MATRICES_H

#include <cstdint>
#include <cstring>
#include <iostream>
#include <ostream>

#include "glad.h"
#include "strings.h"
#include "vec.h"

struct FMat2x2 {
  using type = float;
  inline static constexpr size_t kDimension = 2;
  inline static constexpr size_t kCardinality = 4;

  float v[4];

  FMat2x2() = default;

  explicit FMat2x2(float value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static FMat2x2 Zero() {
    FMat2x2 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static FMat2x2 Identity() {
    FMat2x2 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  FMat2x2(const float* vec) { std::memcpy(v, vec, sizeof(v)); }

  FMat2x2 operator+(const FMat2x2& rhs) const {
    FMat2x2 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  FMat2x2& operator+=(const FMat2x2& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  FMat2x2 operator-(const FMat2x2& rhs) const {
    FMat2x2 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  FMat2x2& operator-=(const FMat2x2& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  FMat2x2& operator=(const FMat2x2& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  FMat2x2(const FMat2x2& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  FMat2x2(FMat2x2&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  FMat2x2& operator=(FMat2x2&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  FMat2x2& operator*=(float val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  FMat2x2 operator*(float val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  FVec2 operator*(const FVec2& val) const {
    FVec2 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  FMat2x2& operator*=(FMat2x2 val) {
    *this = *this * val;
    return *this;
  }

  float val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  float& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  FMat2x2 operator*(FMat2x2 val) const {
    FMat2x2 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const FMat2x2& rhs) const {
    constexpr float kEpsilon = 1e-10;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const FMat2x2& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const FMat2x2& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }

  void AsOpenglUniform(GLint location) const {
    glUniformMatrix2fv(location, 1, GL_TRUE, v);
  }
};

struct FMat3x3 {
  using type = float;
  inline static constexpr size_t kDimension = 3;
  inline static constexpr size_t kCardinality = 9;

  float v[9];

  FMat3x3() = default;

  explicit FMat3x3(float value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static FMat3x3 Zero() {
    FMat3x3 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static FMat3x3 Identity() {
    FMat3x3 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  FMat3x3(const float* vec) { std::memcpy(v, vec, sizeof(v)); }

  FMat3x3 operator+(const FMat3x3& rhs) const {
    FMat3x3 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  FMat3x3& operator+=(const FMat3x3& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  FMat3x3 operator-(const FMat3x3& rhs) const {
    FMat3x3 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  FMat3x3& operator-=(const FMat3x3& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  FMat3x3& operator=(const FMat3x3& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  FMat3x3(const FMat3x3& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  FMat3x3(FMat3x3&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  FMat3x3& operator=(FMat3x3&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  FMat3x3& operator*=(float val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  FMat3x3 operator*(float val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  FVec3 operator*(const FVec3& val) const {
    FVec3 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  FMat3x3& operator*=(FMat3x3 val) {
    *this = *this * val;
    return *this;
  }

  float val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  float& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  FMat3x3 operator*(FMat3x3 val) const {
    FMat3x3 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const FMat3x3& rhs) const {
    constexpr float kEpsilon = 1e-10;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const FMat3x3& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const FMat3x3& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }

  void AsOpenglUniform(GLint location) const {
    glUniformMatrix3fv(location, 1, GL_TRUE, v);
  }
};

struct FMat4x4 {
  using type = float;
  inline static constexpr size_t kDimension = 4;
  inline static constexpr size_t kCardinality = 16;

  float v[16];

  FMat4x4() = default;

  explicit FMat4x4(float value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static FMat4x4 Zero() {
    FMat4x4 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static FMat4x4 Identity() {
    FMat4x4 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  FMat4x4(const float* vec) { std::memcpy(v, vec, sizeof(v)); }

  FMat4x4 operator+(const FMat4x4& rhs) const {
    FMat4x4 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  FMat4x4& operator+=(const FMat4x4& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  FMat4x4 operator-(const FMat4x4& rhs) const {
    FMat4x4 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  FMat4x4& operator-=(const FMat4x4& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  FMat4x4& operator=(const FMat4x4& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  FMat4x4(const FMat4x4& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  FMat4x4(FMat4x4&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  FMat4x4& operator=(FMat4x4&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  FMat4x4& operator*=(float val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  FMat4x4 operator*(float val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  FVec4 operator*(const FVec4& val) const {
    FVec4 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  FMat4x4& operator*=(FMat4x4 val) {
    *this = *this * val;
    return *this;
  }

  float val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  float& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  FMat4x4 operator*(FMat4x4 val) const {
    FMat4x4 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const FMat4x4& rhs) const {
    constexpr float kEpsilon = 1e-10;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const FMat4x4& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const FMat4x4& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }

  void AsOpenglUniform(GLint location) const {
    glUniformMatrix4fv(location, 1, GL_TRUE, v);
  }
};

struct DMat2x2 {
  using type = double;
  inline static constexpr size_t kDimension = 2;
  inline static constexpr size_t kCardinality = 4;

  double v[4];

  DMat2x2() = default;

  explicit DMat2x2(double value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static DMat2x2 Zero() {
    DMat2x2 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static DMat2x2 Identity() {
    DMat2x2 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  DMat2x2(const double* vec) { std::memcpy(v, vec, sizeof(v)); }

  DMat2x2 operator+(const DMat2x2& rhs) const {
    DMat2x2 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  DMat2x2& operator+=(const DMat2x2& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  DMat2x2 operator-(const DMat2x2& rhs) const {
    DMat2x2 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  DMat2x2& operator-=(const DMat2x2& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  DMat2x2& operator=(const DMat2x2& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  DMat2x2(const DMat2x2& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  DMat2x2(DMat2x2&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  DMat2x2& operator=(DMat2x2&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  DMat2x2& operator*=(double val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  DMat2x2 operator*(double val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  DVec2 operator*(const DVec2& val) const {
    DVec2 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  DMat2x2& operator*=(DMat2x2 val) {
    *this = *this * val;
    return *this;
  }

  double val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  double& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  DMat2x2 operator*(DMat2x2 val) const {
    DMat2x2 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const DMat2x2& rhs) const {
    constexpr double kEpsilon = 1e-10;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const DMat2x2& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const DMat2x2& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }

  void AsOpenglUniform(GLint location) const {
    glUniformMatrix2dv(location, 1, GL_TRUE, v);
  }
};

struct DMat3x3 {
  using type = double;
  inline static constexpr size_t kDimension = 3;
  inline static constexpr size_t kCardinality = 9;

  double v[9];

  DMat3x3() = default;

  explicit DMat3x3(double value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static DMat3x3 Zero() {
    DMat3x3 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static DMat3x3 Identity() {
    DMat3x3 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  DMat3x3(const double* vec) { std::memcpy(v, vec, sizeof(v)); }

  DMat3x3 operator+(const DMat3x3& rhs) const {
    DMat3x3 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  DMat3x3& operator+=(const DMat3x3& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  DMat3x3 operator-(const DMat3x3& rhs) const {
    DMat3x3 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  DMat3x3& operator-=(const DMat3x3& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  DMat3x3& operator=(const DMat3x3& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  DMat3x3(const DMat3x3& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  DMat3x3(DMat3x3&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  DMat3x3& operator=(DMat3x3&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  DMat3x3& operator*=(double val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  DMat3x3 operator*(double val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  DVec3 operator*(const DVec3& val) const {
    DVec3 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  DMat3x3& operator*=(DMat3x3 val) {
    *this = *this * val;
    return *this;
  }

  double val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  double& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  DMat3x3 operator*(DMat3x3 val) const {
    DMat3x3 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const DMat3x3& rhs) const {
    constexpr double kEpsilon = 1e-10;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const DMat3x3& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const DMat3x3& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }

  void AsOpenglUniform(GLint location) const {
    glUniformMatrix3dv(location, 1, GL_TRUE, v);
  }
};

struct DMat4x4 {
  using type = double;
  inline static constexpr size_t kDimension = 4;
  inline static constexpr size_t kCardinality = 16;

  double v[16];

  DMat4x4() = default;

  explicit DMat4x4(double value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static DMat4x4 Zero() {
    DMat4x4 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static DMat4x4 Identity() {
    DMat4x4 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  DMat4x4(const double* vec) { std::memcpy(v, vec, sizeof(v)); }

  DMat4x4 operator+(const DMat4x4& rhs) const {
    DMat4x4 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  DMat4x4& operator+=(const DMat4x4& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  DMat4x4 operator-(const DMat4x4& rhs) const {
    DMat4x4 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  DMat4x4& operator-=(const DMat4x4& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  DMat4x4& operator=(const DMat4x4& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  DMat4x4(const DMat4x4& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  DMat4x4(DMat4x4&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  DMat4x4& operator=(DMat4x4&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  DMat4x4& operator*=(double val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  DMat4x4 operator*(double val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  DVec4 operator*(const DVec4& val) const {
    DVec4 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  DMat4x4& operator*=(DMat4x4 val) {
    *this = *this * val;
    return *this;
  }

  double val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  double& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  DMat4x4 operator*(DMat4x4 val) const {
    DMat4x4 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const DMat4x4& rhs) const {
    constexpr double kEpsilon = 1e-10;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const DMat4x4& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const DMat4x4& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }

  void AsOpenglUniform(GLint location) const {
    glUniformMatrix4dv(location, 1, GL_TRUE, v);
  }
};

struct IMat2x2 {
  using type = int;
  inline static constexpr size_t kDimension = 2;
  inline static constexpr size_t kCardinality = 4;

  int v[4];

  IMat2x2() = default;

  explicit IMat2x2(int value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static IMat2x2 Zero() {
    IMat2x2 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static IMat2x2 Identity() {
    IMat2x2 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  IMat2x2(const int* vec) { std::memcpy(v, vec, sizeof(v)); }

  IMat2x2 operator+(const IMat2x2& rhs) const {
    IMat2x2 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  IMat2x2& operator+=(const IMat2x2& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  IMat2x2 operator-(const IMat2x2& rhs) const {
    IMat2x2 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  IMat2x2& operator-=(const IMat2x2& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  IMat2x2& operator=(const IMat2x2& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  IMat2x2(const IMat2x2& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  IMat2x2(IMat2x2&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  IMat2x2& operator=(IMat2x2&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  IMat2x2& operator*=(int val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  IMat2x2 operator*(int val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  IVec2 operator*(const IVec2& val) const {
    IVec2 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  IMat2x2& operator*=(IMat2x2 val) {
    *this = *this * val;
    return *this;
  }

  int val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  int& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  IMat2x2 operator*(IMat2x2 val) const {
    IMat2x2 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const IMat2x2& rhs) const {
    constexpr int kEpsilon = 0;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const IMat2x2& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const IMat2x2& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }
};

struct IMat3x3 {
  using type = int;
  inline static constexpr size_t kDimension = 3;
  inline static constexpr size_t kCardinality = 9;

  int v[9];

  IMat3x3() = default;

  explicit IMat3x3(int value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static IMat3x3 Zero() {
    IMat3x3 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static IMat3x3 Identity() {
    IMat3x3 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  IMat3x3(const int* vec) { std::memcpy(v, vec, sizeof(v)); }

  IMat3x3 operator+(const IMat3x3& rhs) const {
    IMat3x3 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  IMat3x3& operator+=(const IMat3x3& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  IMat3x3 operator-(const IMat3x3& rhs) const {
    IMat3x3 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  IMat3x3& operator-=(const IMat3x3& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  IMat3x3& operator=(const IMat3x3& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  IMat3x3(const IMat3x3& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  IMat3x3(IMat3x3&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  IMat3x3& operator=(IMat3x3&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  IMat3x3& operator*=(int val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  IMat3x3 operator*(int val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  IVec3 operator*(const IVec3& val) const {
    IVec3 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  IMat3x3& operator*=(IMat3x3 val) {
    *this = *this * val;
    return *this;
  }

  int val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  int& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  IMat3x3 operator*(IMat3x3 val) const {
    IMat3x3 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const IMat3x3& rhs) const {
    constexpr int kEpsilon = 0;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const IMat3x3& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const IMat3x3& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }
};

struct IMat4x4 {
  using type = int;
  inline static constexpr size_t kDimension = 4;
  inline static constexpr size_t kCardinality = 16;

  int v[16];

  IMat4x4() = default;

  explicit IMat4x4(int value) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] = value;
    }
  }

  static IMat4x4 Zero() {
    IMat4x4 result;
    std::memset(result.v, 0, sizeof(result.v));
    return result;
  }

  static IMat4x4 Identity() {
    IMat4x4 result;
    std::memset(result.v, 0, sizeof(result.v));
    for (size_t i = 0; i < kDimension; ++i) result.v[i * kDimension + i] = 1;
    return result;
  }

  IMat4x4(const int* vec) { std::memcpy(v, vec, sizeof(v)); }

  IMat4x4 operator+(const IMat4x4& rhs) const {
    IMat4x4 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] += rhs.v[i];
    }
    return result;
  }

  IMat4x4& operator+=(const IMat4x4& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] += rhs.v[i];
    }
    return *this;
  }

  IMat4x4 operator-(const IMat4x4& rhs) const {
    IMat4x4 result = *this;
    for (size_t i = 0; i < kCardinality; ++i) {
      result.v[i] -= rhs.v[i];
    }
    return result;
  }

  IMat4x4& operator-=(const IMat4x4& rhs) {
    for (size_t i = 0; i < kCardinality; ++i) {
      v[i] -= rhs.v[i];
    }
    return *this;
  }

  IMat4x4& operator=(const IMat4x4& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  IMat4x4(const IMat4x4& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  IMat4x4(IMat4x4&& rhs) { std::memcpy(v, rhs.v, sizeof(rhs.v)); }

  IMat4x4& operator=(IMat4x4&& rhs) {
    std::memcpy(v, rhs.v, sizeof(rhs.v));
    return *this;
  }

  IMat4x4& operator*=(int val) {
    for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
    return *this;
  }

  IMat4x4 operator*(int val) const {
    auto result = *this;
    for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
    return result;
  }

  IVec4 operator*(const IVec4& val) const {
    IVec4 result;
    for (size_t i = 0; i < kDimension; ++i) {
      result.v[i] = 0;
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i] += v[i * kDimension + j] * val.v[j];
      }
    }
    return result;
  }

  IMat4x4& operator*=(IMat4x4 val) {
    *this = *this * val;
    return *this;
  }

  int val(size_t i, size_t j) const { return v[i * kDimension + j]; }

  int& mut(size_t i, size_t j) { return v[i * kDimension + j]; }

  IMat4x4 operator*(IMat4x4 val) const {
    IMat4x4 result;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        result.v[i * kDimension + j] = 0;
        for (size_t k = 0; k < kDimension; ++k) {
          result.v[i * kDimension + j] +=
              v[i * kDimension + k] * val.v[k * kDimension + j];
        }
      }
    }
    return result;
  }

  bool operator==(const IMat4x4& rhs) const {
    constexpr int kEpsilon = 0;
    for (size_t i = 0; i < kDimension; ++i) {
      for (size_t j = 0; j < kDimension; ++j) {
        if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) >
            kEpsilon) {
          return false;
        }
      }
    }
    return true;
  }

  bool operator!=(const IMat4x4& rhs) const { return !(*this == rhs); }

  friend std::ostream& operator<<(std::ostream& os, const IMat4x4& v) {
    os << "{ ";
    for (size_t row = 0; row < kDimension; ++row) {
      os << "{ ";
      for (size_t col = 0; col < kDimension; ++col) {
        os << v.v[row * kDimension + col];
        if (col + 1 < kDimension) os << ", ";
      }
      os << " }";
      if (row + 1 < kDimension) os << ", ";
    }
    return os << " }";
  }

  void AppendToString(std::string& sink) const {
    sink.append("{ ");
    for (size_t row = 0; row < kDimension; ++row) {
      sink.append("{ ");
      for (size_t col = 0; col < kDimension; ++col) {
        StrAppend(&sink, v[row * kDimension + col]);
        if (col + 1 < kDimension) sink.append(", ");
      }
      sink.append(" }");
      if (row + 1 < kDimension) sink.append(", ");
    }
    sink.append(" }");
  }
};

#endif  // _GAME_MATRICES_H
