# ChromaFiler

ChromaFiler is an experimental file manager with a unique interface, borrowing ideas from [Spatial file managers](https://en.wikipedia.org/wiki/Spatial_file_manager) and [Miller Column browsers](https://en.wikipedia.org/wiki/Miller_columns) and taking inspiration from the classic Mac OS Finder.

<img src="https://user-images.githubusercontent.com/8228102/201300188-1f07c66d-759b-45a9-aa70-34a8fe2a531a.gif" width="706" alt="An animation demonstrating folder navigation using ChromaFiler">

It functions similar to a column-view browser, but each column can be broken off into its own window by dragging and dropping. You can use it as a popup menu docked to your taskbar for quickly locating a file, or as a complete replacement for Windows File Explorer (and Notepad).

ChromaFiler works on Windows 8 through 11 (and [mostly](https://github.com/vanjac/chromafiler/issues?q=is%3Aopen+label%3Awindows7+label%3Abug) works on Windows 7).

## Download

Check the [Releases](https://github.com/vanjac/chromafiler/releases) page for the latest beta build. See [installation instructions](https://github.com/vanjac/chromafiler/wiki/Installation) for additional information.

ChromaFiler is still in development, so be sure to turn on the [auto check for updates](https://github.com/vanjac/chromafiler/wiki/Settings#updateabout) feature so you'll be notified of new releases.

## Tutorial

See the [wiki](https://github.com/vanjac/chromafiler/wiki/Tutorial) for an introduction to the app.

## Building

Building requires the Windows SDK (install version 10.0.19041.0 through the [Visual Studio Installer](https://visualstudio.microsoft.com/downloads/)) and [Visual Studio Code](https://code.visualstudio.com/). The installer is built using [NSIS](https://nsis.sourceforge.io/Main_Page). (You will need to [clone all submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules#_cloning_submodules).)

VS Code must be launched from the "x64 Native Tools Command Prompt" (search in Start menu) to access the correct MSVC build tools. Type `code` in this prompt to launch VS Code. Open the ChromaFiler directory, then open `src/main.cpp` and press `Ctrl+Shift+B` to build the app in the Debug configuration.

## Suggested pairings

- [Everything](https://www.voidtools.com/) by voidtools (recommend installing with folder context menus)
- [Microsoft PowerToys](https://github.com/microsoft/PowerToys) (see notes on [preview handlers](https://github.com/vanjac/chromafiler/wiki/Installation#preview-handlers))

## Contact me:

For questions / feedback / bug reports please [email me](https://chroma.zone/contact)
