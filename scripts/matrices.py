import jinja2


def main():
    template = """
    struct {{typename}} {
      using type = {{type}};
      inline static constexpr size_t kDimension = {{dimension}};
      inline static constexpr size_t kCardinality = {{cardinality}};

      {{type}} v[{{cardinality}}];
      
      {{typename}}() = default;

      explicit {{typename}}({{type}} value) {
        for (size_t i = 0; i < kCardinality; ++i) {
          v[i] = value;
        }
      }

      static {{typename}} Zero() {
        {{typename}} result;
        std::memset(result.v, 0, sizeof(result.v));
        return result;
      }

      static {{typename}} Identity() {
        {{typename}} result;
        std::memset(result.v, 0, sizeof(result.v));
        for (size_t i = 0; i < kDimension; ++i)
          result.v[i * kDimension + i] = 1;
        return result;
      }
      
      {{typename}}(const {{type}}* vec) {
        std::memcpy(v, vec, sizeof(v));
      }

      {{typename}} operator+(const {{typename}}& rhs) const  {
        {{typename}} result = *this;
        for (size_t i = 0; i < kCardinality; ++i) {
            result.v[i] += rhs.v[i];
        }
        return result;
      }

      {{typename}}& operator+=(const {{typename}}& rhs) {
        for (size_t i = 0; i < kCardinality; ++i) {
            v[i] += rhs.v[i];
        }
        return *this;
      }

      {{typename}} operator-(const {{typename}}& rhs) const {
        {{typename}} result = *this;
        for (size_t i = 0; i < kCardinality; ++i) {
            result.v[i] -= rhs.v[i];
        }
        return result;
      }

      {{typename}}& operator-=(const {{typename}}& rhs) {
        for (size_t i = 0; i < kCardinality; ++i) {
            v[i] -= rhs.v[i];
        }
        return *this;
      }

      {{typename}}& operator=(const {{typename}}& rhs) {
        std::memcpy(v, rhs.v, sizeof(rhs.v));
        return *this;
      }

      {{typename}}(const {{typename}}& rhs) {
        std::memcpy(v, rhs.v, sizeof(rhs.v));
      }

      {{typename}}({{typename}}&& rhs) {
        std::memcpy(v, rhs.v, sizeof(rhs.v));
      }

      {{typename}}& operator=({{typename}}&& rhs) {
        std::memcpy(v, rhs.v, sizeof(rhs.v));
        return *this;
      }

      {{typename}}& operator*=({{type}} val) {
        for (size_t i = 0; i < kCardinality; ++i) v[i] *= val;
        return *this;
      }

      {{typename}} operator*({{type}} val) const {
        auto result = *this;
        for (size_t i = 0; i < kCardinality; ++i) result.v[i] *= val;
        return result;
      }

      {{vector}} operator*(const {{vector}}& val) const {
        {{vector}} result;
        for (size_t i = 0; i < kDimension; ++i) {
          result.v[i] = 0;
          for (size_t j = 0; j < kDimension; ++j) {
            result.v[i] += v[i * kDimension + j] * val.v[j];
          }
        }
        return result;
      }

      {{typename}}& operator*=({{typename}} val) {
        *this = *this * val;
        return *this;
      }

      {{type}} val(size_t i, size_t j) const {
        return v[i * kDimension + j];
      }

      {{type}}& mut(size_t i, size_t j) {
        return v[i * kDimension + j];
      }

      {{typename}} operator*({{typename}} val) const {
        {{typename}} result;
        for (size_t i = 0; i < kDimension; ++i) {
          for (size_t j = 0; j < kDimension; ++j) {
            result.v[i * kDimension + j] = 0;
            for (size_t k = 0; k < kDimension; ++k) {
              result.v[i * kDimension + j] += v[i * kDimension + k] * val.v[k * kDimension + j];
            }
          }
        }
        return result;
      }

      bool operator==(const {{typename}}& rhs) const {
        constexpr {{type}} kEpsilon = {% if type == "int" %} 0 {% else %} 1e-10 {% endif %};
        for (size_t i = 0; i < kDimension; ++i) {
          for (size_t j = 0; j < kDimension; ++j) {
            if (std::abs(v[i * kDimension + j] - rhs.v[i * kDimension + j]) > kEpsilon) {
              return false;
            }
          }
        }
        return true;
      }

      bool operator!=(const {{typename}}& rhs) const {
        return !(*this == rhs);
      }

      friend std::ostream& operator<<(std::ostream& os, const {{typename}}& v) {
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

      {% if type != "int" %}
      void AsOpenglUniform(GLint location) const {
        {{uniform}}(location, 1, GL_TRUE, v);
      }
      {%endif%}
    };
    """
    template = jinja2.Template(template, trim_blocks=True, lstrip_blocks=True)
    print("// DO NOT EDIT - EDIT WITH matrices.py SCRIPT INSTEAD.")
    print("#pragma once")
    print("#ifndef _GAME_MATRICES_H")
    print("#define _GAME_MATRICES_H")
    print("")
    print("#include <cstdint>")
    print("#include <iostream>")
    print("#include <cstring>")
    print("#include <ostream>")
    print("#include \"glad.h\"")
    print("#include \"strings.h\"")
    print("#include \"vec.h\"")
    openglLetter = {
        "float": "f",
        "double": "d",
        "int": "f"
    }
    vectorName = {
        "float": "FVec",
        "double": "DVec",
        "int": "IVec"
    }
    for type in ["float", "double", "int"]:
        for components in range(2, 5):
            typename = type[0].upper() + "Mat" + \
                str(components) + "x" + str(components)
            vector = vectorName[type] + str(components)
            print(template.render(typename=typename, type=type, cardinality=components ** 2,
                  uniform="glUniformMatrix" + str(components) + openglLetter[type] + "v", dimension=components, vector=vector))
    print("")
    print("#endif  // _GAME_MATRICES_H")


if __name__ == '__main__':
    main()
