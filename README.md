# Garimpo

A fast, light file search and replace tool for macOS and Linux. Search folders by text, regular
expression or boolean expression, browse every match with a live preview, and replace safely with
automatic backups.

Garimpo is written in C++ with Qt Widgets and is built to stay small: the result list only paints
the rows on screen, matches are stored compactly, and a fixed pool of worker threads reads at most
one file each. A search over 13,000 source files with 66,000 matches runs in under half a second
and the app stays under 100 MB of memory with all those results open.

## Features

- **Search** one or more folders (`;`-separated) with include/exclude file masks, folders to skip,
  subfolders, hidden files, and size and date filters.
- **Pattern modes**
  - *Text*: literal match, optionally case-sensitive and whole-word.
  - *Regex*: Perl-compatible regular expressions (PCRE2), Unicode-aware.
  - *Expression*: terms combined with `AND`, `OR`, `NOT` (or `&`, `|`, `!`, `-`), parentheses,
    `"exact phrases"` and `/regex/` terms. Adjacent terms are ANDed. Evaluated per line or per file.
  - An empty pattern lists every file accepted by the masks; *File names only* matches names instead.
- **Results** grouped by file with highlighted matches, a path filter and sorting, plus a preview
  with line numbers that scrolls to the selected match. F3 / Shift+F3 (⌘G / ⇧⌘G on macOS) move
  between matches, Ctrl+Down / Ctrl+Up between files. Return or a double-click opens the editor at
  the matching line.
- **Tabs**: every search opens in a tab that can be renamed, reordered, searched again, or saved
  as a `.garimpo` file and reopened. Open tabs come back on the next launch.
- **Replace** with a preview of every changed line, per-file and per-line selection, regex groups
  (`$1`, `${name}`), original encoding and line endings preserved, and **Undo Last Replace**.
- **Documents**: optionally search inside DOCX, ODT, RTF and PDF files. PDF text comes from
  `pdftotext` (poppler) when it is installed. Extracted text is cached and refreshed when the file
  changes.
- **Memory**: folders, patterns, replacements and masks are remembered per field; favorite
  folders can be pinned. The form reopens with the last search.
- **Encodings**: UTF-8, UTF-16 with BOM and Windows-1252 are detected; binary files are skipped.
- **Export** to text (`path:line:column: text`), CSV or HTML, and print.
- **Command-line tool** `garimpo` sharing the same engine.

## Build

Requirements: CMake 3.21+, a C++20 compiler, Qt 6.4+ (Core, Gui, Widgets, PrintSupport, Test) and zlib.

macOS (Homebrew):

```sh
brew install qtbase cmake ninja
brew install poppler            # optional, for PDF search
scripts/build.sh                # build/Garimpo.app
scripts/build.sh --install      # also copy it to /Applications
```

The app links to the Homebrew Qt libraries. `scripts/build.sh --standalone` copies Qt into the
bundle so it runs on a Mac without Homebrew.

Debian / Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev zlib1g-dev poppler-utils
scripts/build.sh --install      # installs garimpo-gui, garimpo and a desktop entry into ~/.local
```

Fedora:

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel zlib-devel poppler-utils
scripts/build.sh --install
```

`-DGARIMPO_BUILD_APP=OFF` builds only the command-line tool (Qt Core only, no display needed).

## Command line

```sh
garimpo search ~/Projects -p "TODO" -m "*.cpp;*.md"
garimpo search ~/Projects -x -p 'invoice AND (paid OR "due date") NOT draft' --file-scope -l
garimpo search ~/Docs -p "contract" --docs --json > results.garimpo
garimpo replace ~/Projects -e -p 'oldName\((\w+)\)' -r 'newName($1)' --dry-run
garimpo undo
garimpo cache clean
```

On macOS the tool is inside the bundle: `ln -s /Applications/Garimpo.app/Contents/Helpers/garimpo ~/.local/bin/garimpo`.
Run `garimpo --help` for every option.

## Settings and data

- **Editor command** (Settings ▸ General) runs in your login shell with `{file}`, `{line}` and
  `{column}` replaced, e.g. `code -g {file}:{line}:{column}`. When empty, files open in their
  default app.
- History, session, document cache and replace backups live in
  `~/Library/Application Support/Garimpo` (macOS) or `~/.local/share/Garimpo` (Linux).
  `GARIMPO_HOME` points them elsewhere.

## Project layout

```
src/core     search engine: walker, matchers, expressions, decoding, documents, replace, storage
src/app      Qt Widgets app
src/cli      command-line tool
tests        core test suite (Qt Test)
resources    macOS bundle files, icon and Linux desktop entry
scripts      build script
```
