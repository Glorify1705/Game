// DO NOT EDIT - EDIT WITH vectors.py SCRIPT INSTEAD.
#pragma once
#ifndef _GAME_VEC_H
#define _GAME_VEC_H

#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>

#include "glad.h"

namespace G {

struct FVec2 {
  using type = float;
  inline static constexpr size_t kCardinality = 2;

  union {
    struct {
      float x;
      float y;
    };
    float v[2];
  };

  FVec2() = default;

  static FVec2 Zero() {
    FVec2 result;
    result.x = 0;
    result.y = 0;
    return result;
  }

  explicit FVec2(float value) {
    x = value;
    y = value;
  }

  explicit FVec2(float x, float y) {
    this->x = x;
    this->y = y;
  }

  FVec2(const float* vec) {
    this->x = vec[0];
    this->y = vec[1];
  }

  FVec2 operator+(const FVec2& rhs) const {
    FVec2 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    return result;
  }

  FVec2& operator+=(const FVec2& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }

  FVec2 operator-(const FVec2& rhs) const {
    FVec2 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    return result;
  }

  FVec2 operator-() const { return *this * -1; }

  FVec2& operator-=(const FVec2& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }

  FVec2& operator=(const FVec2& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  FVec2(const FVec2& rhs) {
    x = rhs.x;
    y = rhs.y;
  }

  FVec2(FVec2&& rhs) {
    x = rhs.x;
    y = rhs.y;
  }

  FVec2 operator*(float rhs) const {
    FVec2 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    return result;
  }

  FVec2 operator/(float rhs) const {
    FVec2 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    return result;
  }

  FVec2& operator*=(float rhs) {
    x *= rhs;
    y *= rhs;
    return *this;
  }

  FVec2& operator=(FVec2&& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  float Dot(const FVec2& rhs) const {
    float result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    return result;
  }

  bool operator==(const FVec2& rhs) const {
    constexpr float kEpsilon = 1e-10;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const FVec2& rhs) const { return !(*this == rhs); }

  float Length2() const { return Dot(*this); }

  float Length() const { return std::sqrt(Length2()); }
  FVec2 Normalized() const { return *this * (1.0 / Length()); }

  friend std::ostream& operator<<(std::ostream& os, const FVec2& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    return os << " }";
  }

  friend void AppendToString(const FVec2& v, std::string& sink) {
    char buf[32] = {0};
    size_t len = snprintf(buf, sizeof(buf), "{ %.3f, %.3f }", v.x, v.y);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const { glUniform2f(location, x, y); }
};

inline FVec2 FVec(float x, float y) { return FVec2(x, y); }

struct FVec3 {
  using type = float;
  inline static constexpr size_t kCardinality = 3;

  union {
    struct {
      float x;
      float y;
      float z;
    };
    float v[3];
  };

  FVec3() = default;

  static FVec3 Zero() {
    FVec3 result;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    return result;
  }

  explicit FVec3(float value) {
    x = value;
    y = value;
    z = value;
  }

  explicit FVec3(float x, float y, float z) {
    this->x = x;
    this->y = y;
    this->z = z;
  }

  FVec3(const float* vec) {
    this->x = vec[0];
    this->y = vec[1];
    this->z = vec[2];
  }

  FVec3 operator+(const FVec3& rhs) const {
    FVec3 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    result.z += rhs.z;
    return result;
  }

  FVec3& operator+=(const FVec3& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  FVec3 operator-(const FVec3& rhs) const {
    FVec3 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    result.z -= rhs.z;
    return result;
  }

  FVec3 operator-() const { return *this * -1; }

  FVec3& operator-=(const FVec3& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }

  FVec3& operator=(const FVec3& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }

  FVec3(const FVec3& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
  }

  FVec3(FVec3&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
  }

  FVec3 operator*(float rhs) const {
    FVec3 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    result.z *= rhs;
    return result;
  }

  FVec3 operator/(float rhs) const {
    FVec3 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    result.z /= rhs;
    return result;
  }

  FVec3& operator*=(float rhs) {
    x *= rhs;
    y *= rhs;
    z *= rhs;
    return *this;
  }

  FVec3& operator=(FVec3&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }

  float Dot(const FVec3& rhs) const {
    float result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    result += z * rhs.z;
    return result;
  }

  bool operator==(const FVec3& rhs) const {
    constexpr float kEpsilon = 1e-10;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    if (std::abs(z - rhs.z) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const FVec3& rhs) const { return !(*this == rhs); }

  float Length2() const { return Dot(*this); }

  float Length() const { return std::sqrt(Length2()); }
  FVec3 Normalized() const { return *this * (1.0 / Length()); }

  FVec3 Cross(const FVec3& b) const {
    const auto& a = *this;
    return FVec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
  }

  friend std::ostream& operator<<(std::ostream& os, const FVec3& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    os << ", ";
    os << v.z;
    return os << " }";
  }

  friend void AppendToString(const FVec3& v, std::string& sink) {
    char buf[32] = {0};
    size_t len =
        snprintf(buf, sizeof(buf), "{ %.3f, %.3f, %.3f }", v.x, v.y, v.z);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const { glUniform3f(location, x, y, z); }
};

inline FVec3 FVec(float x, float y, float z) { return FVec3(x, y, z); }

struct FVec4 {
  using type = float;
  inline static constexpr size_t kCardinality = 4;

  union {
    struct {
      float x;
      float y;
      float z;
      float w;
    };
    float v[4];
  };

  FVec4() = default;

  static FVec4 Zero() {
    FVec4 result;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    result.w = 0;
    return result;
  }

  explicit FVec4(float value) {
    x = value;
    y = value;
    z = value;
    w = value;
  }

  explicit FVec4(float x, float y, float z, float w) {
    this->x = x;
    this->y = y;
    this->z = z;
    this->w = w;
  }

  FVec4(const float* vec) {
    this->x = vec[0];
    this->y = vec[1];
    this->z = vec[2];
    this->w = vec[3];
  }

  FVec4 operator+(const FVec4& rhs) const {
    FVec4 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    result.z += rhs.z;
    result.w += rhs.w;
    return result;
  }

  FVec4& operator+=(const FVec4& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
  }

  FVec4 operator-(const FVec4& rhs) const {
    FVec4 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    result.z -= rhs.z;
    result.w -= rhs.w;
    return result;
  }

  FVec4 operator-() const { return *this * -1; }

  FVec4& operator-=(const FVec4& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
  }

  FVec4& operator=(const FVec4& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
    return *this;
  }

  FVec4(const FVec4& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
  }

  FVec4(FVec4&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
  }

  FVec4 operator*(float rhs) const {
    FVec4 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    result.z *= rhs;
    result.w *= rhs;
    return result;
  }

  FVec4 operator/(float rhs) const {
    FVec4 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    result.z /= rhs;
    result.w /= rhs;
    return result;
  }

  FVec4& operator*=(float rhs) {
    x *= rhs;
    y *= rhs;
    z *= rhs;
    w *= rhs;
    return *this;
  }

  FVec4& operator=(FVec4&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
    return *this;
  }

  float Dot(const FVec4& rhs) const {
    float result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    result += z * rhs.z;
    result += w * rhs.w;
    return result;
  }

  bool operator==(const FVec4& rhs) const {
    constexpr float kEpsilon = 1e-10;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    if (std::abs(z - rhs.z) > kEpsilon) return false;
    if (std::abs(w - rhs.w) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const FVec4& rhs) const { return !(*this == rhs); }

  float Length2() const { return Dot(*this); }

  float Length() const { return std::sqrt(Length2()); }
  FVec4 Normalized() const { return *this * (1.0 / Length()); }

  friend std::ostream& operator<<(std::ostream& os, const FVec4& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    os << ", ";
    os << v.z;
    os << ", ";
    os << v.w;
    return os << " }";
  }

  friend void AppendToString(const FVec4& v, std::string& sink) {
    char buf[32] = {0};
    size_t len = snprintf(buf, sizeof(buf), "{ %.3f, %.3f, %.3f, %.3f }", v.x,
                          v.y, v.z, v.w);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const {
    glUniform4f(location, x, y, z, w);
  }
};

inline FVec4 FVec(float x, float y, float z, float w) {
  return FVec4(x, y, z, w);
}

struct DVec2 {
  using type = double;
  inline static constexpr size_t kCardinality = 2;

  union {
    struct {
      double x;
      double y;
    };
    double v[2];
  };

  DVec2() = default;

  static DVec2 Zero() {
    DVec2 result;
    result.x = 0;
    result.y = 0;
    return result;
  }

  explicit DVec2(double value) {
    x = value;
    y = value;
  }

  explicit DVec2(double x, double y) {
    this->x = x;
    this->y = y;
  }

  DVec2(const double* vec) {
    this->x = vec[0];
    this->y = vec[1];
  }

  DVec2 operator+(const DVec2& rhs) const {
    DVec2 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    return result;
  }

  DVec2& operator+=(const DVec2& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }

  DVec2 operator-(const DVec2& rhs) const {
    DVec2 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    return result;
  }

  DVec2 operator-() const { return *this * -1; }

  DVec2& operator-=(const DVec2& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }

  DVec2& operator=(const DVec2& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  DVec2(const DVec2& rhs) {
    x = rhs.x;
    y = rhs.y;
  }

  DVec2(DVec2&& rhs) {
    x = rhs.x;
    y = rhs.y;
  }

  DVec2 operator*(double rhs) const {
    DVec2 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    return result;
  }

  DVec2 operator/(double rhs) const {
    DVec2 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    return result;
  }

  DVec2& operator*=(double rhs) {
    x *= rhs;
    y *= rhs;
    return *this;
  }

  DVec2& operator=(DVec2&& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  double Dot(const DVec2& rhs) const {
    double result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    return result;
  }

  bool operator==(const DVec2& rhs) const {
    constexpr double kEpsilon = 1e-10;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const DVec2& rhs) const { return !(*this == rhs); }

  double Length2() const { return Dot(*this); }

  double Length() const { return std::sqrt(Length2()); }
  DVec2 Normalized() const { return *this * (1.0 / Length()); }

  friend std::ostream& operator<<(std::ostream& os, const DVec2& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    return os << " }";
  }

  friend void AppendToString(const DVec2& v, std::string& sink) {
    char buf[32] = {0};
    size_t len = snprintf(buf, sizeof(buf), "{ %.3lf, %.3lf }", v.x, v.y);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const { glUniform2d(location, x, y); }
};

inline DVec2 DVec(double x, double y) { return DVec2(x, y); }

struct DVec3 {
  using type = double;
  inline static constexpr size_t kCardinality = 3;

  union {
    struct {
      double x;
      double y;
      double z;
    };
    double v[3];
  };

  DVec3() = default;

  static DVec3 Zero() {
    DVec3 result;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    return result;
  }

  explicit DVec3(double value) {
    x = value;
    y = value;
    z = value;
  }

  explicit DVec3(double x, double y, double z) {
    this->x = x;
    this->y = y;
    this->z = z;
  }

  DVec3(const double* vec) {
    this->x = vec[0];
    this->y = vec[1];
    this->z = vec[2];
  }

  DVec3 operator+(const DVec3& rhs) const {
    DVec3 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    result.z += rhs.z;
    return result;
  }

  DVec3& operator+=(const DVec3& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  DVec3 operator-(const DVec3& rhs) const {
    DVec3 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    result.z -= rhs.z;
    return result;
  }

  DVec3 operator-() const { return *this * -1; }

  DVec3& operator-=(const DVec3& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }

  DVec3& operator=(const DVec3& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }

  DVec3(const DVec3& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
  }

  DVec3(DVec3&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
  }

  DVec3 operator*(double rhs) const {
    DVec3 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    result.z *= rhs;
    return result;
  }

  DVec3 operator/(double rhs) const {
    DVec3 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    result.z /= rhs;
    return result;
  }

  DVec3& operator*=(double rhs) {
    x *= rhs;
    y *= rhs;
    z *= rhs;
    return *this;
  }

  DVec3& operator=(DVec3&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }

  double Dot(const DVec3& rhs) const {
    double result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    result += z * rhs.z;
    return result;
  }

  bool operator==(const DVec3& rhs) const {
    constexpr double kEpsilon = 1e-10;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    if (std::abs(z - rhs.z) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const DVec3& rhs) const { return !(*this == rhs); }

  double Length2() const { return Dot(*this); }

  double Length() const { return std::sqrt(Length2()); }
  DVec3 Normalized() const { return *this * (1.0 / Length()); }

  DVec3 Cross(const DVec3& b) const {
    const auto& a = *this;
    return DVec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
  }

  friend std::ostream& operator<<(std::ostream& os, const DVec3& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    os << ", ";
    os << v.z;
    return os << " }";
  }

  friend void AppendToString(const DVec3& v, std::string& sink) {
    char buf[32] = {0};
    size_t len =
        snprintf(buf, sizeof(buf), "{ %.3lf, %.3lf, %.3lf }", v.x, v.y, v.z);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const { glUniform3d(location, x, y, z); }
};

inline DVec3 DVec(double x, double y, double z) { return DVec3(x, y, z); }

struct DVec4 {
  using type = double;
  inline static constexpr size_t kCardinality = 4;

  union {
    struct {
      double x;
      double y;
      double z;
      double w;
    };
    double v[4];
  };

  DVec4() = default;

  static DVec4 Zero() {
    DVec4 result;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    result.w = 0;
    return result;
  }

  explicit DVec4(double value) {
    x = value;
    y = value;
    z = value;
    w = value;
  }

  explicit DVec4(double x, double y, double z, double w) {
    this->x = x;
    this->y = y;
    this->z = z;
    this->w = w;
  }

  DVec4(const double* vec) {
    this->x = vec[0];
    this->y = vec[1];
    this->z = vec[2];
    this->w = vec[3];
  }

  DVec4 operator+(const DVec4& rhs) const {
    DVec4 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    result.z += rhs.z;
    result.w += rhs.w;
    return result;
  }

  DVec4& operator+=(const DVec4& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
  }

  DVec4 operator-(const DVec4& rhs) const {
    DVec4 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    result.z -= rhs.z;
    result.w -= rhs.w;
    return result;
  }

  DVec4 operator-() const { return *this * -1; }

  DVec4& operator-=(const DVec4& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
  }

  DVec4& operator=(const DVec4& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
    return *this;
  }

  DVec4(const DVec4& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
  }

  DVec4(DVec4&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
  }

  DVec4 operator*(double rhs) const {
    DVec4 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    result.z *= rhs;
    result.w *= rhs;
    return result;
  }

  DVec4 operator/(double rhs) const {
    DVec4 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    result.z /= rhs;
    result.w /= rhs;
    return result;
  }

  DVec4& operator*=(double rhs) {
    x *= rhs;
    y *= rhs;
    z *= rhs;
    w *= rhs;
    return *this;
  }

  DVec4& operator=(DVec4&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
    return *this;
  }

  double Dot(const DVec4& rhs) const {
    double result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    result += z * rhs.z;
    result += w * rhs.w;
    return result;
  }

  bool operator==(const DVec4& rhs) const {
    constexpr double kEpsilon = 1e-10;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    if (std::abs(z - rhs.z) > kEpsilon) return false;
    if (std::abs(w - rhs.w) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const DVec4& rhs) const { return !(*this == rhs); }

  double Length2() const { return Dot(*this); }

  double Length() const { return std::sqrt(Length2()); }
  DVec4 Normalized() const { return *this * (1.0 / Length()); }

  friend std::ostream& operator<<(std::ostream& os, const DVec4& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    os << ", ";
    os << v.z;
    os << ", ";
    os << v.w;
    return os << " }";
  }

  friend void AppendToString(const DVec4& v, std::string& sink) {
    char buf[32] = {0};
    size_t len = snprintf(buf, sizeof(buf), "{ %.3lf, %.3lf, %.3lf, %.3lf }",
                          v.x, v.y, v.z, v.w);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const {
    glUniform4d(location, x, y, z, w);
  }
};

inline DVec4 DVec(double x, double y, double z, double w) {
  return DVec4(x, y, z, w);
}

struct IVec2 {
  using type = int;
  inline static constexpr size_t kCardinality = 2;

  union {
    struct {
      int x;
      int y;
    };
    int v[2];
  };

  IVec2() = default;

  static IVec2 Zero() {
    IVec2 result;
    result.x = 0;
    result.y = 0;
    return result;
  }

  explicit IVec2(int value) {
    x = value;
    y = value;
  }

  explicit IVec2(int x, int y) {
    this->x = x;
    this->y = y;
  }

  IVec2(const int* vec) {
    this->x = vec[0];
    this->y = vec[1];
  }

  IVec2 operator+(const IVec2& rhs) const {
    IVec2 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    return result;
  }

  IVec2& operator+=(const IVec2& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }

  IVec2 operator-(const IVec2& rhs) const {
    IVec2 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    return result;
  }

  IVec2 operator-() const { return *this * -1; }

  IVec2& operator-=(const IVec2& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }

  IVec2& operator=(const IVec2& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  IVec2(const IVec2& rhs) {
    x = rhs.x;
    y = rhs.y;
  }

  IVec2(IVec2&& rhs) {
    x = rhs.x;
    y = rhs.y;
  }

  IVec2 operator*(int rhs) const {
    IVec2 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    return result;
  }

  IVec2 operator/(int rhs) const {
    IVec2 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    return result;
  }

  IVec2& operator*=(int rhs) {
    x *= rhs;
    y *= rhs;
    return *this;
  }

  IVec2& operator=(IVec2&& rhs) {
    x = rhs.x;
    y = rhs.y;
    return *this;
  }

  int Dot(const IVec2& rhs) const {
    int result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    return result;
  }

  bool operator==(const IVec2& rhs) const {
    constexpr int kEpsilon = 0;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const IVec2& rhs) const { return !(*this == rhs); }

  int Length2() const { return Dot(*this); }

  friend std::ostream& operator<<(std::ostream& os, const IVec2& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    return os << " }";
  }

  friend void AppendToString(const IVec2& v, std::string& sink) {
    char buf[32] = {0};
    size_t len = snprintf(buf, sizeof(buf), "{ %d, %d }", v.x, v.y);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const { glUniform2f(location, x, y); }
};

inline IVec2 IVec(int x, int y) { return IVec2(x, y); }

struct IVec3 {
  using type = int;
  inline static constexpr size_t kCardinality = 3;

  union {
    struct {
      int x;
      int y;
      int z;
    };
    int v[3];
  };

  IVec3() = default;

  static IVec3 Zero() {
    IVec3 result;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    return result;
  }

  explicit IVec3(int value) {
    x = value;
    y = value;
    z = value;
  }

  explicit IVec3(int x, int y, int z) {
    this->x = x;
    this->y = y;
    this->z = z;
  }

  IVec3(const int* vec) {
    this->x = vec[0];
    this->y = vec[1];
    this->z = vec[2];
  }

  IVec3 operator+(const IVec3& rhs) const {
    IVec3 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    result.z += rhs.z;
    return result;
  }

  IVec3& operator+=(const IVec3& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  IVec3 operator-(const IVec3& rhs) const {
    IVec3 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    result.z -= rhs.z;
    return result;
  }

  IVec3 operator-() const { return *this * -1; }

  IVec3& operator-=(const IVec3& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }

  IVec3& operator=(const IVec3& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }

  IVec3(const IVec3& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
  }

  IVec3(IVec3&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
  }

  IVec3 operator*(int rhs) const {
    IVec3 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    result.z *= rhs;
    return result;
  }

  IVec3 operator/(int rhs) const {
    IVec3 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    result.z /= rhs;
    return result;
  }

  IVec3& operator*=(int rhs) {
    x *= rhs;
    y *= rhs;
    z *= rhs;
    return *this;
  }

  IVec3& operator=(IVec3&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    return *this;
  }

  int Dot(const IVec3& rhs) const {
    int result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    result += z * rhs.z;
    return result;
  }

  bool operator==(const IVec3& rhs) const {
    constexpr int kEpsilon = 0;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    if (std::abs(z - rhs.z) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const IVec3& rhs) const { return !(*this == rhs); }

  int Length2() const { return Dot(*this); }

  IVec3 Cross(const IVec3& b) const {
    const auto& a = *this;
    return IVec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                 a.x * b.y - a.y * b.x);
  }

  friend std::ostream& operator<<(std::ostream& os, const IVec3& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    os << ", ";
    os << v.z;
    return os << " }";
  }

  friend void AppendToString(const IVec3& v, std::string& sink) {
    char buf[32] = {0};
    size_t len = snprintf(buf, sizeof(buf), "{ %d, %d, %d }", v.x, v.y, v.z);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const { glUniform3f(location, x, y, z); }
};

inline IVec3 IVec(int x, int y, int z) { return IVec3(x, y, z); }

struct IVec4 {
  using type = int;
  inline static constexpr size_t kCardinality = 4;

  union {
    struct {
      int x;
      int y;
      int z;
      int w;
    };
    int v[4];
  };

  IVec4() = default;

  static IVec4 Zero() {
    IVec4 result;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    result.w = 0;
    return result;
  }

  explicit IVec4(int value) {
    x = value;
    y = value;
    z = value;
    w = value;
  }

  explicit IVec4(int x, int y, int z, int w) {
    this->x = x;
    this->y = y;
    this->z = z;
    this->w = w;
  }

  IVec4(const int* vec) {
    this->x = vec[0];
    this->y = vec[1];
    this->z = vec[2];
    this->w = vec[3];
  }

  IVec4 operator+(const IVec4& rhs) const {
    IVec4 result = *this;
    result.x += rhs.x;
    result.y += rhs.y;
    result.z += rhs.z;
    result.w += rhs.w;
    return result;
  }

  IVec4& operator+=(const IVec4& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    w += rhs.w;
    return *this;
  }

  IVec4 operator-(const IVec4& rhs) const {
    IVec4 result = *this;
    result.x -= rhs.x;
    result.y -= rhs.y;
    result.z -= rhs.z;
    result.w -= rhs.w;
    return result;
  }

  IVec4 operator-() const { return *this * -1; }

  IVec4& operator-=(const IVec4& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    w -= rhs.w;
    return *this;
  }

  IVec4& operator=(const IVec4& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
    return *this;
  }

  IVec4(const IVec4& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
  }

  IVec4(IVec4&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
  }

  IVec4 operator*(int rhs) const {
    IVec4 result = *this;
    result.x *= rhs;
    result.y *= rhs;
    result.z *= rhs;
    result.w *= rhs;
    return result;
  }

  IVec4 operator/(int rhs) const {
    IVec4 result = *this;
    result.x /= rhs;
    result.y /= rhs;
    result.z /= rhs;
    result.w /= rhs;
    return result;
  }

  IVec4& operator*=(int rhs) {
    x *= rhs;
    y *= rhs;
    z *= rhs;
    w *= rhs;
    return *this;
  }

  IVec4& operator=(IVec4&& rhs) {
    x = rhs.x;
    y = rhs.y;
    z = rhs.z;
    w = rhs.w;
    return *this;
  }

  int Dot(const IVec4& rhs) const {
    int result = 0;
    result += x * rhs.x;
    result += y * rhs.y;
    result += z * rhs.z;
    result += w * rhs.w;
    return result;
  }

  bool operator==(const IVec4& rhs) const {
    constexpr int kEpsilon = 0;
    if (std::abs(x - rhs.x) > kEpsilon) return false;
    if (std::abs(y - rhs.y) > kEpsilon) return false;
    if (std::abs(z - rhs.z) > kEpsilon) return false;
    if (std::abs(w - rhs.w) > kEpsilon) return false;
    return true;
  }

  bool operator!=(const IVec4& rhs) const { return !(*this == rhs); }

  int Length2() const { return Dot(*this); }

  friend std::ostream& operator<<(std::ostream& os, const IVec4& v) {
    os << "{ ";
    os << v.x;
    os << ", ";
    os << v.y;
    os << ", ";
    os << v.z;
    os << ", ";
    os << v.w;
    return os << " }";
  }

  friend void AppendToString(const IVec4& v, std::string& sink) {
    char buf[32] = {0};
    size_t len =
        snprintf(buf, sizeof(buf), "{ %d, %d, %d, %d }", v.x, v.y, v.z, v.w);
    sink.append(buf, len);
  }

  void AsOpenglUniform(GLint location) const {
    glUniform4f(location, x, y, z, w);
  }
};

inline IVec4 IVec(int x, int y, int z, int w) { return IVec4(x, y, z, w); }

}  // namespace G

#endif  // _GAME_VEC_H
