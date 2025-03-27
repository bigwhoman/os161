-- In your lsp.lua file, replace Mason with direct configuration
return {
  -- LSP Configuration (no Mason)
  {
    "neovim/nvim-lspconfig",
    event = { "BufReadPre", "BufNewFile" },
    config = function()
      local lspconfig = require("lspconfig")

      -- Configure clangd from Nix
      lspconfig.clangd.setup({
        cmd = { "clangd", "--background-index", "--clang-tidy" },  -- Use system clangd
        -- rest of your config...
      })

      -- Key bindings remain the same
      -- ...
    end,
  },

  -- DAP Configuration (use system-provided debuggers)
  {
    "mfussenegger/nvim-dap",
    dependencies = { "rcarriga/nvim-dap-ui" },
    config = function()
      local dap = require("dap")
      
      -- Configure system codelldb
      dap.adapters.codelldb = {
        type = "server",
        port = "${port}",
        executable = {
          command = "codelldb",  -- Use system codelldb
          args = { "--port", "${port}" },
        },
      }
      
      -- Rest of DAP config...
    end,
  },

  -- For null-ls, use system-provided formatters/linters
  
}
