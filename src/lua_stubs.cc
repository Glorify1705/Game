#include "lua_stubs.h"

#include <cstdio>
#include <cstring>

#include "error.h"

namespace G {
namespace {

const char* StripArgPrefix(const char* name) {
  if (name == nullptr) return nullptr;
  for (const char* p = name; *p; ++p) {
    if (*p == ':') return p + 1;
  }
  return name;
}

const char* ResolveType(const char* t, const char* self_alias) {
  if (t == nullptr) return "any";
  if (strcmp(t, "self") == 0) return self_alias;
  return t;
}

// Write library function stubs (G.graphics.draw, etc.).
void WriteFunctionStubs(FILE* f, const LuaLibraryDef* defs, size_t def_count) {
  fprintf(f, "---@class G\n");
  for (size_t d = 0; d < def_count; ++d) {
    for (size_t i = 0; i < defs[d].library_count; ++i) {
      fprintf(f, "---@field %s G.%s\n", defs[d].libraries[i].name,
              defs[d].libraries[i].name);
    }
  }
  fprintf(f, "G = {}\n\n");

  for (size_t d = 0; d < def_count; ++d) {
    for (size_t i = 0; i < defs[d].library_count; ++i) {
      const auto& lib = defs[d].libraries[i];
      fprintf(f, "---@class G.%s\n", lib.name);
      fprintf(f, "G.%s = {}\n\n", lib.name);

      for (size_t j = 0; j < lib.count; ++j) {
        const auto& func = lib.funcs[j];
        if (func.name == nullptr) continue;

        if (func.docstring != nullptr) {
          fprintf(f, "---%s\n", func.docstring);
        }

        for (size_t k = 0; k < func.args.argc; ++k) {
          const auto& arg = func.args[k];
          const char* name = StripArgPrefix(arg.name);
          if (arg.type != nullptr) {
            fprintf(f, "---@param %s %s %s\n", name, arg.type,
                    arg.docs ? arg.docs : "");
          } else if (name != nullptr) {
            fprintf(f, "---@param %s any %s\n", name, arg.docs ? arg.docs : "");
          }
        }

        for (size_t k = 0; k < func.returns.argc; ++k) {
          const auto& ret = func.returns[k];
          if (ret.type != nullptr) {
            fprintf(f, "---@return %s %s %s\n", ret.type,
                    ret.name ? ret.name : "", ret.docs ? ret.docs : "");
          } else if (ret.name != nullptr) {
            fprintf(f, "---@return any %s %s\n", ret.name,
                    ret.docs ? ret.docs : "");
          }
        }

        fprintf(f, "function G.%s.%s(", lib.name, func.name);
        for (size_t k = 0; k < func.args.argc; ++k) {
          if (k > 0) fprintf(f, ", ");
          fprintf(f, "%s", StripArgPrefix(func.args[k].name));
        }
        fprintf(f, ") end\n\n");
      }
    }
  }
}

// Write userdata type class stubs (Vec2, Body, etc.).
void WriteUserdataStubs(FILE* f, const LuaLibraryDef* defs, size_t def_count) {
  for (size_t d = 0; d < def_count; ++d) {
    for (size_t i = 0; i < defs[d].type_count; ++i) {
      const auto& type = defs[d].types[i];
      const char* alias = type.luals_alias;
      if (type.docstring != nullptr) {
        fprintf(f, "---%s\n", type.docstring);
      }
      fprintf(f, "---@class %s\n", alias);

      for (size_t j = 0; j < type.field_count; ++j) {
        const auto& field = type.fields[j];
        fprintf(f, "---@field %s %s %s\n", field.name,
                ResolveType(field.type, alias), field.docs ? field.docs : "");
      }

      for (size_t j = 0; j < type.operator_count; ++j) {
        const auto& op = type.operators[j];
        if (op.operand_type != nullptr) {
          fprintf(f, "---@operator %s(%s): %s\n", op.op,
                  ResolveType(op.operand_type, alias),
                  ResolveType(op.return_type, alias));
        } else {
          fprintf(f, "---@operator %s: %s\n", op.op,
                  ResolveType(op.return_type, alias));
        }
      }

      fprintf(f, "local %s = {}\n\n", alias);

      for (size_t j = 0; j < type.method_count; ++j) {
        const auto& method = type.methods[j];
        if (method.docstring != nullptr) {
          fprintf(f, "---%s\n", method.docstring);
        }

        for (size_t k = 0; k < method.params.argc; ++k) {
          const auto& param = method.params[k];
          fprintf(f, "---@param %s %s %s\n", param.name,
                  ResolveType(param.type, alias), param.docs ? param.docs : "");
        }

        for (size_t k = 0; k < method.returns.argc; ++k) {
          const auto& ret = method.returns[k];
          fprintf(f, "---@return %s %s %s\n", ResolveType(ret.type, alias),
                  ret.name ? ret.name : "", ret.docs ? ret.docs : "");
        }

        fprintf(f, "function %s:%s(", alias, method.name);
        for (size_t k = 0; k < method.params.argc; ++k) {
          if (k > 0) fprintf(f, ", ");
          fprintf(f, "%s", method.params[k].name);
        }
        fprintf(f, ") end\n\n");
      }
    }
  }
}

}  // namespace

void WriteLuaLSStubs(const char* output_path, const LuaLibraryDef* defs,
                     size_t def_count) {
  FILE* f = fopen(output_path, "w");
  CHECK(f != nullptr, "Could not open ", output_path, " for writing");

  fprintf(f, "---@meta\n");
  fprintf(f, "-- Auto-generated LuaLS stubs from LuaApiFunction metadata.\n");
  fprintf(f, "-- Do not edit manually.\n\n");

  WriteFunctionStubs(f, defs, def_count);
  WriteUserdataStubs(f, defs, def_count);

  fclose(f);
}

}  // namespace G
