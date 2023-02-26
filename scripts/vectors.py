import jinja2


def main():
    template = """
    struct {{typename}} {
      using type = {{type}};
      inline static constexpr size_t kCardinality = {{components|length}};

      union {
          struct {
            {% for c in components %}
                {{type}} {{c}};
            {% endfor %}
          };
          {{type}} v[{{components|length}}];
      };

      {{typename}}() = default;

      static {{typename}} Zero() {
        {{typename}} result;
        {% for c in components %}
            result.{{c}} = 0;
        {% endfor %}
        return result;
      }

      explicit {{typename}}({{type}} value) {
        {% for c in components %}
            {{c}} = value;
        {% endfor %}
      }

      explicit {{typename}}(
        {% for c in components %}
          {{type}} {{c}}
          {% if not loop.last %},{% endif %}
        {% endfor %}) {
          {% for c in components %}
              this->{{c}} = {{c}};
          {% endfor %}
      }

      {{typename}}(const {{type}}* vec) {
          {% for c in components %}
              this->{{c}} = vec[{{loop.index0}}];
          {% endfor %}
      }

      {{typename}} operator+(const {{typename}}& rhs) const  {
        {{typename}} result = *this;
        {% for c in components %}
            result.{{c}} += rhs.{{c}};
        {% endfor %}
        return result;
      }

      {{typename}}& operator+=(const {{typename}}& rhs) {
        {% for c in components %}
            {{c}} += rhs.{{c}};
        {% endfor %}
        return *this;
      }

      {{typename}} operator-(const {{typename}}& rhs) const {
        {{typename}} result = *this;
        {% for c in components %}
            result.{{c}} -= rhs.{{c}};
        {% endfor %}
        return result;
      }

      {{typename}} operator-() const {
        return *this * -1;
      }

      {{typename}}& operator-=(const {{typename}}& rhs) {
        {% for c in components %}
            {{c}} -= rhs.{{c}};
        {% endfor %}
        return *this;
      }

      {{typename}}& operator=(const {{typename}}& rhs) {
        {% for c in components %}
            {{c}} = rhs.{{c}};
        {% endfor %}
        return *this;
      }

      {{typename}}(const {{typename}}& rhs) {
        {% for c in components %}
            {{c}} = rhs.{{c}};
        {% endfor %}
      }

      {{typename}}({{typename}}&& rhs) {
        {% for c in components %}
            {{c}} = rhs.{{c}};
        {% endfor %}
      }

      {{typename}} operator*({{type}} rhs) const {
        {{typename}} result = *this;
        {% for c in components %}
            result.{{c}} *= rhs;
        {% endfor %}
        return result;
      }
      
      {{typename}} operator/({{type}} rhs) const {
        {{typename}} result = *this;
        {% for c in components %}
            result.{{c}} /= rhs;
        {% endfor %}
        return result;
      }

      {{typename}}& operator*=({{type}} rhs) {
        {% for c in components %}
            {{c}} *= rhs;
        {% endfor %}
        return *this;
      }

      {{typename}}& operator=({{typename}}&& rhs) {
        {% for c in components %}
            {{c}} = rhs.{{c}};
        {% endfor %}
        return *this;
      }

      {{type}} Dot(const {{typename}}& rhs) const {
        {{type}} result = 0;
        {% for c in components %}
            result += {{c}} * rhs.{{c}};
        {% endfor %}
        return result;
      }
      
      bool operator==(const {{typename}}& rhs) const {
        constexpr {{type}} kEpsilon = {% if type == "int" %} 0 {% else %} 1e-10 {% endif %};
        {% for c in components %}
          if (std::abs({{c}} - rhs.{{c}}) > kEpsilon) return false;
        {% endfor %}
        return true;
      }

      bool operator!=(const {{typename}}& rhs) const {
        return !(*this == rhs);
      }
      
      {{type}} Length2() const { return Dot(*this); }

      {% if type == "float" or type == "double" %}
        {{type}} Length() const { return std::sqrt(Length2()); }
        {{typename}} Normalized() const { return *this * (1.0 / Length()); }
      {% endif %}

      {% if components | length == 3 %}
      {{typename}} Cross(const {{typename}}& b) const {
        const auto& a = *this;
        return {{typename}}(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); 
      }
      {% endif %}

      friend std::ostream& operator<<(std::ostream& os, const {{typename}}& v) {
        os << "{ ";
        {% for c in components %}
            os << v.{{c}};
            {% if not loop.last %}
                os << ", ";
            {% endif %}
        {% endfor %}
        return os << " }";
      }

      void AppendToString(std::string& sink) const {
        char buf[32] = {0};
        size_t len = snprintf(buf, sizeof(buf), "{ {% for c in components %}{{format}}{%if not loop.last %}, {%endif%}{% endfor %} }", {% for c in components %}
          {{c}}
          {%if not loop.last %}, {%endif%}
        {%endfor%});
        sink.append(buf, len);
      }

      void AsOpenglUniform(GLint location) const {
        {{uniform}}(location, {% for c in components %} {{c}} {% if not loop.last %},{%endif%} {% endfor%});
      }
    };

    inline {{typename}} {{letter}}Vec(
            {% for c in components %}
                {{type}} {{c}}
                            {% if not loop.last %},{%endif%}
            {% endfor %}
    ) {
      return {{typename}}(            {% for c in components %}
                {{c}}
                            {% if not loop.last %},{%endif%}
            {% endfor %});
    }
    """
    template = jinja2.Template(template, trim_blocks=True, lstrip_blocks=True)
    names = ["x", "y", "z", "w"]
    print("// DO NOT EDIT - EDIT WITH vectors.py SCRIPT INSTEAD.")
    print("#pragma once")
    print("#ifndef _GAME_VEC_H")
    print("#define _GAME_VEC_H")
    print("")
    print("#include <cstdint>")
    print("#include <string>")
    print("#include <cmath>")
    print("#include <ostream>")
    print("#include \"glad.h\"")
    formats = {
        "float": "%.3f",
        "double": "%.3lf",
        "int": "%d"
    }
    openglLetter = {
        "float": "f",
        "double": "d",
        "int": "f"
    }
    for type in ["float", "double", "int"]:
        for components in range(2, 5):
            typename = type[0].upper() + "Vec" + str(components)
            print(template.render(typename=typename, type=type, uniform="glUniform" + str(components) + openglLetter[type],
                  format=formats[type], components=names[:components], letter=type[0].upper()))
    print("")
    print("#endif  // _GAME_VEC_H")


if __name__ == '__main__':
    main()
