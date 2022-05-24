# chromabrowse

*(working title)*

An experimental file manager for Windows, designed to combine aspects of [Spatial file managers](https://en.wikipedia.org/wiki/Spatial_file_manager) and [Miller columns](https://en.wikipedia.org/wiki/Miller_columns), and taking some inspiration from the [Finder](https://en.wikipedia.org/wiki/Finder_(software)). This is a work in progress.

![Screenshot](https://user-images.githubusercontent.com/8228102/169676491-aa43b0b1-1a0a-48ae-aa62-f28ccf35fe23.png)

chromabrowse works on Windows 10 and 11. Windows 7 support will be added eventually.

## Download

Check the [Releases](https://github.com/vanjac/chromabrowse/releases) page for the latest alpha build. **May be unstable, use at your own risk!**

## Tutorial

See the [wiki](https://github.com/vanjac/chromabrowse/wiki/Tutorial) for an introduction to the app.

## Building

Building requires the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) and [Visual Studio Code](https://code.visualstudio.com/).

VS Code must be launched from the "x64 Native Tools Command Prompt" (search in Start menu) to access the correct MSVC build tools. Type `code` in this prompt to launch VS Code. Open the chromabrowse directory, then open `src/main.cpp` and press `Ctrl+Shift+B` to build the app.

## Suggested pairings

- [Everything](https://www.voidtools.com/) by voidtools
- [Microsoft PowerToys](https://github.com/microsoft/PowerToys)
