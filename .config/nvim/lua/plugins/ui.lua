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
}
