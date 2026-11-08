# Faloya в других редакторах

Компилятор сам является языковым сервером: `faloya lsp` общается по
протоколу Language Server Protocol через stdin и stdout, так что любой
редактор с LSP-клиентом может его использовать. Для VS Code есть
[расширение для VS Code из каталога vscode](../vscode/README.md),
которое запускает сервер за вас. Для Neovim, Helix и Emacs достаточно
приведённой ниже конфигурации.

Каждый фрагмент запускает `faloya lsp`, предполагая, что компилятор
есть в PATH; если это не так, замените `faloya` полным путём к
исполняемому файлу. Стандартную библиотеку сервер ищет так же, как
`faloya check` (переменная `FALOYA_STDLIB` или каталог `stdlib/` рядом
с исполняемым файлом). Для компилятора, который лежит не в своём
исходном дереве, добавьте к команде `--stdlib` и каталог:
`faloya lsp --stdlib /path/to/faloya/stdlib`.

Каждый открытый файл анализируется как отдельная программа вместе с
файлами, которые он импортирует. Чтобы модуль проверялся как часть
программы, которая его импортирует (и ошибки этой программы
показывались в нём), укажите входной файл программы в параметрах
инициализации клиента, абсолютным путём:
`{ "entryFiles": ["/path/to/project/main.fal"] }`. В приведённых
фрагментах показано, куда вставлять эти параметры.

Семантические токены сервера раскрашивают имена по тому, во что они
разрешаются (функции, типы, поля, варианты, параметры, переменные).
Они не покрывают ключевые слова, строки и комментарии, а
TextMate-грамматика входит только в расширение для VS Code из каталога
vscode.

## Neovim

Neovim 0.11 или новее, в `init.lua`:

```lua
vim.filetype.add({ extension = { fal = 'faloya' } })
vim.lsp.config('faloya', {
  cmd = { 'faloya', 'lsp' },
  filetypes = { 'faloya' },
  root_markers = { '.git' },
  -- init_options = { entryFiles = { '/path/to/project/main.fal' } },
})
vim.lsp.enable('faloya')
```

На Neovim 0.10 запускайте сервер вместо этого по событию FileType:

```lua
vim.filetype.add({ extension = { fal = 'faloya' } })
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'faloya',
  callback = function(ev)
    vim.lsp.start({ name = 'faloya', cmd = { 'faloya', 'lsp' }, root_dir = vim.fs.root(ev.buf, { '.git' }) })
  end,
})
```

По умолчанию Neovim показывает диагностику, подсказки при наведении
(`K`), переход к определению (`CTRL-]`), ссылки (`grr`), переименование
(`grn`), символы документа (`gO`), помощь по сигнатуре (`CTRL-S` в
режиме вставки), автодополнение (`CTRL-X CTRL-O` или по мере набора с
`vim.lsp.completion.enable`) и семантическую подсветку. Для inlay hints
нужен `vim.lsp.inlay_hint.enable()`, для фолдинга - `foldmethod=expr`
и `foldexpr=v:lua.vim.lsp.foldexpr()`, а для code lenses -
`vim.lsp.codelens.refresh()` (вызывать, например, на `BufEnter` и
`InsertLeave`). Линзы над обобщёнными функциями показывают число их
инстанциаций. Чтобы получить список инстанциаций через
`vim.lsp.codelens.run()`, дайте Neovim эту команду и сообщите серверу,
что клиент ею располагает:

```lua
vim.lsp.commands['faloya.showInstantiations'] = function(command)
  vim.ui.select(command.arguments[2], { prompt = 'Instantiations of ' .. command.arguments[1] }, function() end)
end
-- and in vim.lsp.config('faloya', ...):
--   init_options = { commands = { 'faloya.showInstantiations' } },
```

## Helix

В `languages.toml` (в каталоге конфигурации Helix или в `.helix/`
внутри проекта):

```toml
[language-server.faloya]
command = "faloya"
args = ["lsp"]
# config = { entryFiles = ["/path/to/project/main.fal"] }

[[language]]
name = "faloya"
scope = "source.faloya"
file-types = ["fal"]
roots = [".git"]
comment-token = "//"
indent = { tab-width = 4, unit = "    " }
language-servers = ["faloya"]
```

(`config` - это то, что Helix отправляет как параметры инициализации.)

Helix показывает диагностику, подсказки при наведении (`space k`),
переход к определению (`gd`), ссылки (`gr`), переименование
(`space r`), символы документа и рабочего пространства (`space s`,
`space S`), помощь по сигнатуре и автодополнение по мере набора, а
inlay hints - при `display-inlay-hints = true` в секции
`[editor.lsp]` файла `config.toml`. Helix подсвечивает код
грамматиками tree-sitter, а у Faloya такой грамматики нет. В Helix
также нет семантических токенов, фолдинга и code lenses, поэтому файлы
Faloya отображаются без раскраски.

## Emacs

С Eglot (встроен начиная с Emacs 29), в `init.el`:

```elisp
(define-derived-mode faloya-mode prog-mode "Faloya"
  (setq-local comment-start "// "))
(add-to-list 'auto-mode-alist '("\\.fal\\'" . faloya-mode))
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs '(faloya-mode "faloya" "lsp")))
(add-hook 'faloya-mode-hook #'eglot-ensure)
```

Для входных файлов серверная программа принимает параметры после
команды:
`'(faloya-mode "faloya" "lsp" :initializationOptions (:entryFiles ["/path/to/project/main.fal"]))`.

Eglot показывает диагностику через Flymake, подсказки при наведении и
помощь по сигнатуре в ElDoc, переход к определению (`M-.`), ссылки
(`M-?`), переименование (`eglot-rename`), символы документа в Imenu,
символы рабочего пространства (`xref-find-apropos`), автодополнение
(`completion-at-point`) и inlay hints; свежий Eglot из GNU ELPA
подсвечивает код и семантическими токенами
(`eglot-semantic-tokens-mode`). Фолдинга и code lenses в Eglot нет.

## Форматирование

`faloya format` переписывает файлы в стандартном виде: отступы, пробелы
между токенами, пустые строки и завершающие запятые; переводы строк
остаются на своих местах. Как в gofmt, типы и знаки `=` в серии
объявлений (подряд идущие `const`, поля структуры) выстраиваются в
колонки. Команда принимает файлы и каталоги (каталог означает файлы
`.fal` внутри него), а без аргументов или с `-` форматирует
стандартный ввод в стандартный вывод. Файл, который не удаётся
разобрать, остаётся без изменений, его ошибки печатаются, код возврата
2; `faloya format --check` ничего не пишет и перечисляет
неотформатированные файлы (код возврата 1) для шага CI или
commit-хука.

Языковой сервер тоже форматирует (`textDocument/formatting`), беря
текст из редактора, а не файл с диска, и возвращает правки только для
тех строк, которые меняются, так что курсор и метки на остальных
строках сохраняются. Документ, который не удаётся разобрать, правок не
получает; его диагностика объясняет почему.

В Neovim форматирует `vim.lsp.buf.format()`; чтобы форматировать при
сохранении, добавьте в приведённую выше конфигурацию:

```lua
vim.api.nvim_create_autocmd('BufWritePre', {
  pattern = '*.fal',
  callback = function() vim.lsp.buf.format() end,
})
```

В Helix форматирует `:format`, а `auto-format = true` в приведённой
выше записи `[[language]]` включает форматирование при сохранении.

В Emacs форматирует `eglot-format-buffer`; чтобы форматировать при
сохранении:

```elisp
(add-hook 'faloya-mode-hook
          (lambda () (add-hook 'before-save-hook #'eglot-format-buffer nil t)))
```

Без языкового сервера редактор может вместо этого прогонять буфер
через `faloya format -`: например, с
[conform.nvim](https://github.com/stevearc/conform.nvim) в Neovim,

```lua
require('conform').setup({
  formatters = { faloya = { command = 'faloya', args = { 'format', '-' }, stdin = true } },
  formatters_by_ft = { faloya = { 'faloya' } },
  format_on_save = { timeout_ms = 2000 },
})
```

или `formatter = { command = "faloya", args = ["format", "-"] }` в
записи `[[language]]` Helix.
