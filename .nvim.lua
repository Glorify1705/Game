vim.lsp.config("lua_ls", {
  cmd = { "lua-language-server" },
  settings = {
    Lua = {
      runtime = { version = "Lua 5.1" },
    },
  },
})

vim.lsp.enable("lua_ls")
