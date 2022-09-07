# chromafile

chromafile is an experimental file manager with a unique interface, borrowing ideas from [Spatial file managers](https://en.wikipedia.org/wiki/Spatial_file_manager) and [Miller Column browsers](https://en.wikipedia.org/wiki/Miller_columns).

![Screenshot showing a series of chromafile windows](https://user-images.githubusercontent.com/8228102/181705394-48504968-4526-4e73-a4f1-d4924e41ec00.png)

It functions similar to a column-view browser (eg. [Finder](https://en.wikipedia.org/wiki/Finder_(software))), but each column can be broken off into its own window by dragging and dropping. You can use it as a popup menu docked to your taskbar for quickly locating a file, or as a complete replacement for Windows File Explorer (and Notepad).

chromafile works on Windows 8 through 11. Windows 7 support may be added eventually.

## Download

Check the [Releases](https://github.com/vanjac/chromafile/releases) page for the latest beta build. See [installation instructions](https://github.com/vanjac/chromafile/wiki/Installation) for additional information.

## Tutorial

See the [wiki](https://github.com/vanjac/chromafile/wiki/Tutorial) for an introduction to the app.

## Building

Building requires the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) and [Visual Studio Code](https://code.visualstudio.com/). The installer is built using [NSIS](https://nsis.sourceforge.io/Main_Page). (You will need to [clone all submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules#_cloning_submodules).)

VS Code must be launched from the "x64 Native Tools Command Prompt" (search in Start menu) to access the correct MSVC build tools. Type `code` in this prompt to launch VS Code. Open the chromafile directory, then open `src/main.cpp` and press `Ctrl+Shift+B` to build the app in the Debug configuration.

## Suggested pairings

- [Everything](https://www.voidtools.com/) by voidtools (recommend installing with folder context menus)
- [Microsoft PowerToys](https://github.com/microsoft/PowerToys)
