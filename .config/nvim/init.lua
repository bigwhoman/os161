vim.g.clipboard = {
  name = 'wl-clipboard',
  copy = {
    ['+'] = 'wl-copy',
    ['*'] = 'wl-copy',
  },
  paste = {
    ['+'] = 'wl-paste',
    ['*'] = 'wl-paste',
  },
  cache_enabled = 1,
}

vim.opt.tabstop = 4
vim.opt.shiftwidth = 4
vim.opt.softtabstop = 4
vim.opt.expandtab = true

