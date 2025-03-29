vim.g.mapleader = " "

local keymap = vim.keymap

keymap.set("i", "jk", "<ESC>", { desc = "Exit insert mode with jk" })

-- window management
keymap.set("n", "<leader>sv", "<C-w>v", { desc = "Vertical Split" })
keymap.set("n", "<leader>sh", "<C-w>s", { desc = "Horizontal Split" })
keymap.set("n", "<leader>se", "<C-w>=", { desc = "Equal Split" })
keymap.set("n", "<leader>sv", "<C-w>v", { desc = "Vertical Split" })
keymap.set("n", "<leader>sx", "<C-w>c", { desc = "Close Split" })

keymap.set("n", "<leader>tt", "<cmd>tabnew<CR>", { desc = "New tab" })
keymap.set("n", "<leader>tx", "<cmd>tabclose<CR>", { desc = "Close Tab" })
keymap.set("n", "<leader>tn", "<cmd>tabn<CR>", { desc = "Next Tab" })
keymap.set("n", "<leader>tp", "<cmd>tabp<CR>", { desc = "Prev Tab" })
