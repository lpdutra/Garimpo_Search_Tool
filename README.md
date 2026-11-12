# Garimpo

A fast file search and replace tool for macOS. Search folders by text, regular expression or
boolean expression, browse every match with a live preview, and replace safely with automatic backups.

## Features

- **Search** one or more folders (`;`-separated), with include/exclude file masks, folders to skip,
  subfolders, hidden files, and size and date filters.
- **Pattern modes**
  - *Text*: literal match, optionally case-sensitive and whole-word.
  - *Regex*: ICU regular expressions (`NSRegularExpression`).
  - *Expression*: terms combined with `AND`, `OR`, `NOT` (or `&`, `|`, `!`, `-`), parentheses,
    `"exact phrases"` and `/regex/` terms. Adjacent terms are ANDed. Expressions are evaluated per line
    or per file.
  - Empty pattern lists every file accepted by the masks; *File names only* matches names instead of contents.
- **Results** grouped by file with highlighted matches, a filter and sorting, plus a preview with
  line numbers that scrolls to the selected match. `⌘G`/`⇧⌘G` move between matches, `⌥⌘↓`/`⌥⌘↑` between
  files. Return or a double-click opens the file in your editor at the matching line.
- **Tabs**: every search opens in a tab that can be renamed, reordered, searched again or saved as a
  `.garimpo` file and reopened later. Open tabs are restored on the next launch.
- **Replace** with a preview of every changed line, per-file and per-line selection, regex groups
  (`$1`), original encoding and line endings preserved, and **Undo Last Replace** from backups.
- **Documents**: optionally search inside PDF, RTF, DOC, DOCX, ODT and web archives. Extracted text is
  cached and refreshed when the file changes.
- **Memory**: folders, patterns, replacements and masks you use are remembered per field, and favorite
  folders can be pinned. The form reopens with your last search.
- **Encodings**: UTF-8, UTF-16 (with BOM), Windows-1252 and Latin-1 are detected automatically;
  binary files are skipped.
- **Export** to text (`path:line:column: text`), CSV or HTML, and print.
- **Command-line tool** sharing the same engine.

## Build

Requires macOS 14 or later and Swift 6 (the Command Line Tools are enough; Xcode is optional).

```sh
scripts/build-app.sh            # builds build/Garimpo.app
scripts/build-app.sh --install  # also copies it to /Applications
scripts/test.sh                 # runs the test suite
swift run Garimpo               # runs the app straight from the package
```

## Command line

The app bundle contains the `garimpo` tool. To use it from a terminal:

```sh
ln -s /Applications/Garimpo.app/Contents/Helpers/garimpo ~/.local/bin/garimpo
```

```sh
garimpo search ~/Projects -p "TODO" -m "*.swift;*.md"
garimpo search ~/Projects -x -p 'invoice AND (paid OR "due date") NOT draft' --file-scope -l
garimpo search ~/Docs -p "contract" --docs --json > results.garimpo
garimpo replace ~/Projects -e -p 'oldName\((\w+)\)' -r 'newName($1)' --dry-run
garimpo undo
garimpo cache clean
```

Run `garimpo --help` for every option.

## Settings and data

- **Editor command** (Settings ▸ General): runs in your login shell with `{file}`, `{line}` and `{column}`
  replaced, e.g. `code -g {file}:{line}:{column}`. If it is empty, files open in their default app.
- History, session, document cache and replace backups live in `~/Library/Application Support/Garimpo/`.
  Set `GARIMPO_HOME` to use a different folder.

## Project layout

```
Sources/GarimpoCore   search engine, matchers, text decoding, document extraction, replace, persistence
Sources/GarimpoApp    SwiftUI app
Sources/GarimpoCLI    command-line tool
Tests/GarimpoCoreTests
scripts/              app bundle, icon and test scripts
Resources/            Info.plist and app icon
```
