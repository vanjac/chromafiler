# chromafile

An experimental file manager for Windows, designed to combine aspects of [Spatial file managers](https://en.wikipedia.org/wiki/Spatial_file_manager) and [Miller columns](https://en.wikipedia.org/wiki/Miller_columns), and taking some inspiration from the [Finder](https://en.wikipedia.org/wiki/Finder_(software)).

![Screenshot](https://user-images.githubusercontent.com/8228102/181127811-944c357c-617e-4868-b20c-63dccfa26ed8.png)

chromafile works on Windows 8 through 11. Windows 7 support may be added eventually.

## Download

Check the [Releases](https://github.com/vanjac/chromafile/releases) page for the latest alpha build. See [installation instructions](https://github.com/vanjac/chromafile/wiki/Installation) for additional information.

## Tutorial

See the [wiki](https://github.com/vanjac/chromafile/wiki/Tutorial) for an introduction to the app.

## Building

Building requires the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) and [Visual Studio Code](https://code.visualstudio.com/). The installer is built using [NSIS](https://nsis.sourceforge.io/Main_Page). (You will need to [clone all submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules#_cloning_submodules).)

VS Code must be launched from the "x64 Native Tools Command Prompt" (search in Start menu) to access the correct MSVC build tools. Type `code` in this prompt to launch VS Code. Open the chromafile directory, then open `src/main.cpp` and press `Ctrl+Shift+B` to build the app in the Debug configuration.

## Suggested pairings

- [Everything](https://www.voidtools.com/) by voidtools
- [Microsoft PowerToys](https://github.com/microsoft/PowerToys)
