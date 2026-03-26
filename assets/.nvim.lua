local util = require("lspconfig.util")

local root = util.root_pattern(
  ".luarc.json",
  ".luarc.jsonc",
  ".luacheckrc",
  ".stylua.toml",
  ".git"
)(vim.fn.getcwd())

vim.lsp.config("lua_ls", {
  cmd = { vim.env.DEVENV_PROFILE .. "/bin/lua-language-server" },
  root_dir = root,
  on_init = function(client)
    local path = client.workspace_folders and client.workspace_folders[1] and client.workspace_folders[1].name
    if path and (
      vim.uv.fs_stat(path .. "/.luarc.json")
      or vim.uv.fs_stat(path .. "/.luarc.jsonc")
    ) then
      return
    end

    client.config.settings = client.config.settings or {}
    client.config.settings.Lua = vim.tbl_deep_extend("force", client.config.settings.Lua or {}, {
      runtime = { version = "LuaJIT" },
      workspace = {
        checkThirdParty = false,
        library = { vim.env.VIMRUNTIME },
      },
      diagnostics = {
        globals = { "vim" },
      },
    })
  end,
})

vim.lsp.enable("lua_ls")
