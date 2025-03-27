-- ~/.config/nvim/lua/plugins/ui.lua
return {
  -- Colorscheme
  {
    "folke/tokyonight.nvim",
    lazy = false, -- Load during startup
    priority = 1000, -- Load before other plugins
  },
  {

    "loctvl842/monokai-pro.nvim",
    lazy = false,
    priority = 1000,
    config = function()
      vim.cmd([[colorscheme monokai-pro-classic]])
    end,
  }
  ,
  
  -- Status line
  {
    "nvim-lualine/lualine.nvim",
    event = "VeryLazy",
    dependencies = { "nvim-tree/nvim-web-devicons" },
    config = function()
      require("lualine").setup({
        -- Your lualine configuration
      })
    end,
  },
  {
    "nvim-treesitter/nvim-treesitter",
    config = function()
        require("nvim-treesitter.configs").setup({
            ensure_installed = { "c", "lua", "vim", "vimdoc", "query", "bash", "python"},
            auto_install = true,
            highlight = {
                enable = true,
            },
            incremental_selection = {
                enable = true,
                keymaps = {
                    init_selection = "<Leader>si",
                    node_incremental = "<Leader>sa",
                    scope_incremental = "<Leader>sc",
                    node_decremental = "<Leader>sd"
                }
            },
            textobjects = {
                select = {
                  enable = true,

                  -- Automatically jump forward to textobj, similar to targets.vim
                  lookahead = true,

                  keymaps = {
                    -- You can use the capture groups defined in textobjects.scm
                    ["af"] = "@function.outer",
                    ["if"] = "@function.inner",
                    ["ac"] = "@class.outer",
                    -- You can optionally set descriptions to the mappings (used in the desc parameter of
                    -- nvim_buf_set_keymap) which plugins like which-key display
                    ["ic"] = { query = "@class.inner", desc = "Select inner part of a class region" },
                    -- You can also use captures from other query groups like `locals.scm`
                    ["as"] = { query = "@local.scope", query_group = "locals", desc = "Select language scope" },
                  },
                  -- You can choose the select mode (default is charwise 'v')
                  --
                  -- Can also be a function which gets passed a table with the keys
                  -- * query_string: eg '@function.inner'
                  -- * method: eg 'v' or 'o'
                  -- and should return the mode ('v', 'V', or '<c-v>') or a table
                  -- mapping query_strings to modes.
                  selection_modes = {
                    ['@parameter.outer'] = 'v', -- charwise
                    ['@function.outer'] = 'V', -- linewise
                    ['@class.outer'] = '<c-v>', -- blockwise
                  },
                  -- If you set this to `true` (default is `false`) then any textobject is
                  -- extended to include preceding or succeeding whitespace. Succeeding
                  -- whitespace has priority in order to act similarly to eg the built-in
                  -- `ap`.
                  --
                  -- Can also be a function which gets passed a table with the keys
                  -- * query_string: eg '@function.inner'
                  -- * selection_mode: eg 'v'
                  -- and should return true or false
                  include_surrounding_whitespace = true,
               },
            }

        })
    end, 
  },
  {
    "nvim-treesitter/nvim-treesitter-textobjects",
  },
}
