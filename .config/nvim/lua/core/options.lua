-- vim.g.clipboard = {
-- 	name = "wl-clipboard",
-- 	copy = {
-- 		["+"] = "wl-copy",
-- 		["*"] = "wl-copy",
-- 	},
-- 	paste = {
-- 		["+"] = "wl-paste",
-- 		["*"] = "wl-paste",
-- 	},
-- 	cache_enabled = 1,
-- }
vim.opt.clipboard:append("unnamedplus")
vim.opt.tabstop = 2
vim.opt.shiftwidth = 2
vim.opt.softtabstop = 2
vim.opt.expandtab = true
vim.opt.ignorecase = true
vim.opt.smartcase = true
vim.opt.number = true
vim.opt.relativenumber = true
vim.opt.splitbelow = true
vim.opt.splitright = true
vim.opt.wrap = false
vim.opt.scrolloff = 999
vim.opt.virtualedit = "block"
vim.opt.inccommand = "split"
vim.opt.ignorecase = true
vim.opt.termguicolors = true
vim.opt.signcolumn = "yes"
