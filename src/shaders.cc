#include "shaders.h"

#include "units.h"

namespace G {
namespace {

constexpr std::string_view kPrePassVertexShader = R"(
    #version 460 core

    layout (location = 0) in vec3 input_position;
    layout (location = 1) in vec2 input_tex_coord;
    layout (location = 2) in vec2 origin;
    layout (location = 3) in float angle;
    layout (location = 4) in vec4 color;
        
    uniform mat4x4 projection;
    uniform mat4x4 transform;    
    uniform vec4 global_color;

    out vec2 tex_coord;
    out vec4 out_color;
    out vec2 screen_coord;

    mat4 RotateZ(float angle) {
      mat4 result = mat4(1.0);
      result[0][0] = cos(angle);
      result[1][0] = -sin(angle);
      result[0][1] = sin(angle);
      result[1][1] = cos(angle);
      return result;
    }

    mat4 Translate(vec2 pos) {
      mat4 result = mat4(1.0);
      result[3][0] = pos.x;
      result[3][1] = pos.y;
      return result;
    }

    void main() {
        mat4 rotation = Translate(origin) * RotateZ(angle) * Translate(-origin);
        gl_Position = projection * transform * rotation * vec4(input_position, 1.0);
        tex_coord = input_tex_coord;
        out_color = global_color * (color / 256.0);
        screen_coord = input_position.xy;
    }
  )";

constexpr std::string_view kPrePassFragmentShader = R"(
    #version 460 core
    out vec4 frag_color;

    in vec2 tex_coord;
    in vec4 out_color;
    in vec2 screen_coord;

    uniform sampler2D tex;

    void main() {
        vec4 color = texture(tex, tex_coord) * out_color;
        frag_color = color;
    }
  )";

constexpr std::string_view kPostPassVertexShader = R"(
  #version 460 core
  layout (location = 0) in vec2 input_position;
  layout (location = 1) in vec2 input_tex_coord;

  out vec2 tex_coord;

  void main()
  {
      gl_Position = vec4(input_position.x, input_position.y, 0.0, 1.0); 
      tex_coord = input_tex_coord;
  }  
  )";

constexpr std::string_view kPostPassFragmentShader = R"(
  #version 460 core
  out vec4 frag_color;

  in vec2 tex_coord;

  uniform sampler2D screen_texture;

  void main() {
      frag_color = texture(screen_texture, tex_coord);
  }
)";

// SDF fragment shader for distance-field text rendering.
//
// Samples the distance value from the SDF atlas and reconstructs a crisp glyph
// edge using smoothstep. The key idea: dFdx/dFdy measure how fast the distance
// changes across adjacent screen pixels (the screen-space gradient). This tells
// us how "zoomed in" we are on the distance field, which lets us adapt the
// anti-aliasing width so edges are always ~1px soft regardless of render size.
//
// - `dist`:      sampled distance from the SDF texture (0 = outside, 1 =
// inside,
//                0.5 = on the glyph edge)
// - `grad`:      screen-space gradient magnitude — large when text is small on
//                screen (each pixel spans many texels), small when text is
//                large
// - `w`:         half-width of the smoothstep transition band, capped at 0.065
//                to keep edges crisp at large sizes where grad approaches zero
// - `threshold`: shifted inward (below 0.5) at small sizes so thin strokes
//                don't vanish — compensates for the gradient averaging away
//                narrow features
constexpr std::string_view kSDFFragmentShader = R"(
    #version 460 core
    out vec4 frag_color;

    in vec2 tex_coord;
    in vec4 out_color;

    uniform sampler2D tex;

    void main() {
        float dist = texture(tex, tex_coord).r;
        float grad = length(vec2(dFdx(dist), dFdy(dist)));
        // Cap transition width so large text keeps sharp edges. The 0.065 value
        // was empirically tuned: small enough for crisp rendering at 100px+,
        // large enough to avoid aliasing at moderate sizes.
        float w = min(0.5 * grad, 0.065);
        // Shift threshold inward at small sizes to keep thin strokes opaque.
        float threshold = 0.5 - 0.15 * grad;
        float alpha = smoothstep(threshold - w, threshold + w, dist);
        frag_color = vec4(out_color.rgb, out_color.a * alpha);
    }
  )";

constexpr std::string_view kFragmentShaderPreamble = R"(
  #version 460 core
  #line 1
)";

constexpr std::string_view kFragmentShaderPostamble = R"(
  out vec4 frag_color;

  in vec2 tex_coord;
  in vec2 screen_coord;
  in vec4 out_color;

  uniform sampler2D tex;

  void main() { 
      frag_color = effect(out_color, tex, tex_coord, screen_coord);
  }
)";

int GetLineNumber(std::string_view err) {
  auto beg = err.find('(');
  auto end = err.find(')');
  int l = 0;
  for (size_t i = beg + 1; i < end; ++i) {
    l = 10 * l + (err[i] - '0');
  }
  return l;
}

}  // namespace

Shaders::Shaders(Allocator* allocator)
    : allocator_(allocator),
      compiled_shaders_(allocator),
      compiled_programs_(allocator),
      gl_shader_handles_(128, allocator),
      gl_program_handles_(128, allocator) {
  // Ensure we have the basic shaders available.
  MUST(Compile(DbAssets::ShaderType::kVertex, "pre_pass.vert",
               kPrePassVertexShader, kUseCache));
  MUST(Compile(DbAssets::ShaderType::kFragment, "pre_pass.frag",
               kPrePassFragmentShader, kUseCache));
  MUST(Link("pre_pass", "pre_pass.vert", "pre_pass.frag", kUseCache));
  MUST(Compile(DbAssets::ShaderType::kFragment, "sdf.frag", kSDFFragmentShader,
               kUseCache));
  MUST(Link("sdf", "pre_pass.vert", "sdf.frag", kUseCache));
  MUST(Compile(DbAssets::ShaderType::kVertex, "post_pass.vert",
               kPostPassVertexShader, kUseCache));
  MUST(Compile(DbAssets::ShaderType::kFragment, "post_pass.frag",
               kPostPassFragmentShader, kUseCache));
  MUST(Link("post_pass", "post_pass.vert", "post_pass.frag", kUseCache));
}

Shaders::~Shaders() {
  for (GLuint handle : gl_shader_handles_) {
    glDeleteShader(handle);
  }
  for (GLuint handle : gl_program_handles_) {
    glDeleteProgram(handle);
  }
}

ErrorOr<void> Shaders::Compile(DbAssets::ShaderType type, std::string_view name,
                               std::string_view glsl, UseCache use_cache) {
  GLuint shader_idx;
  if (compiled_shaders_.Lookup(name, &shader_idx)) {
    if (use_cache == UseCache::kUseCache) {
      LOG("Ignoring already processed shader ", name);
      return {};
    } else {
      glDeleteShader(shader_idx);
    }
  }
  const GLuint shader_type = type == DbAssets::ShaderType::kVertex
                                 ? GL_VERTEX_SHADER
                                 : GL_FRAGMENT_SHADER;
  const GLuint shader = glCreateShader(shader_type);
  CHECK(shader != 0, "Could not compile shader ", name);
  const char* code = glsl.data();
  size_t size = glsl.size();
  OPENGL_CALL(
      glShaderSource(shader, 1, &code, reinterpret_cast<const GLint*>(&size)));
  OPENGL_CALL(glCompileShader(shader), "Compiling shader ", name, ": ", glsl);
  int success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512] = {0};
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    LOG("Shader compilation failed for ", name, " at line ",
        GetLineNumber(info_log), ": ", info_log);
    return Error::Message("Shader compilation failed");
  }
  LOG("Compiled ",
      (type == DbAssets::ShaderType::kVertex ? "vertex" : "fragment"),
      " shader ", name, " with id ", shader);
  gl_shader_handles_.Push(shader);
  compiled_shaders_.Insert(name, shader);
  return {};
}

ErrorOr<void> Shaders::Load(const DbAssets::Shader& shader) {
  ArenaAllocator scratch(allocator_, Kilobytes(64));
  const size_t total_size = kFragmentShaderPreamble.size() + shader.size +
                            kFragmentShaderPostamble.size() + 1;
  auto* buf = reinterpret_cast<char*>(scratch.Alloc(total_size, /*align=*/1));
  StringBuffer code(buf, total_size);
  code.Append(kFragmentShaderPreamble);
  code.AppendBuffer(shader.contents, shader.size);
  code.Append(kFragmentShaderPostamble);
  TRY(Compile(shader.type, shader.name, code.piece(), kForceCompile));
  std::string_view program_name = shader.name;
  CHECK(ConsumeSuffix(&program_name, ".frag"),
        " cannot hot reload vertex shaders yet.");
  TRY(Link(program_name, "pre_pass.vert", shader.name, kForceCompile));
  return {};
}

ErrorOr<void> Shaders::Link(std::string_view name,
                            std::string_view vertex_shader,
                            std::string_view fragment_shader,
                            UseCache use_cache) {
  GLuint program_id;
  if (compiled_programs_.Lookup(name, &program_id)) {
    if (use_cache == UseCache::kUseCache) {
      return {};
    } else {
      glDeleteProgram(program_id);
    }
  }
  GLuint shader_program = glCreateProgram();
  GLuint vertex, fragment;
  if (!compiled_shaders_.Lookup(vertex_shader, &vertex)) {
    LOG("Could not find vertex shader ", vertex_shader);
    return Error::Message("Could not find vertex shader");
  }
  if (!compiled_shaders_.Lookup(fragment_shader, &fragment)) {
    LOG("Could not find fragment shader ", fragment_shader);
    return Error::Message("Could not find fragment shader");
  }
  CHECK(shader_program != 0, "Could not link shaders into ", name);
  OPENGL_CALL(glAttachShader(shader_program, vertex));
  OPENGL_CALL(glAttachShader(shader_program, fragment));
  glBindFragDataLocation(shader_program, 0, "frag_color");
  glLinkProgram(shader_program);
  int success;
  OPENGL_CALL(glGetProgramiv(shader_program, GL_LINK_STATUS, &success));
  if (!success) {
    char info_log[512] = {0};
    glGetProgramInfoLog(shader_program, sizeof(info_log), nullptr, info_log);
    LOG("Could not link shaders into ", name, ": ", info_log);
    return Error::Message("Shader linking failed");
  }
  LOG("Linked program ", name, " with id ", shader_program,
      " from vertex shader ", vertex, " (", vertex_shader,
      ") and fragment shader ", fragment, " (", fragment_shader, ")");
  gl_program_handles_.Push(shader_program);
  compiled_programs_.Insert(name, shader_program);
  return {};
}

void Shaders::UseProgram(std::string_view program) {
  GLuint program_id;
  CHECK(compiled_programs_.Lookup(program, &program_id),
        " could not find program ", program);
  current_program_ = program_id;
  current_program_name_ = program.data();
  OPENGL_CALL(glUseProgram(current_program_));
}

}  // namespace G
