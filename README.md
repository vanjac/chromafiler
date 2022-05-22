# chromabrowse

An experimental file manager for Windows, designed to combine aspects of [Spatial file managers](https://en.wikipedia.org/wiki/Spatial_file_manager) and [Miller columns](https://en.wikipedia.org/wiki/Miller_columns), and taking some inspiration from the [Finder](https://en.wikipedia.org/wiki/Finder_(software)). This is a work in progress.

chromabrowse has only been tested on Windows 11, but will probably work fine on Windows 10. Windows 7 support will be added eventually.

## Building

Building requires the [Windows SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) and [Visual Studio Code](https://code.visualstudio.com/).

VS Code must be launched from the "x64 Native Tools Command Prompt" (search in Start menu) to access the correct MSVC build tools. Navigate to the chromabrowse directory and type `code .` to launch VS Code. Once this is done, open `src/main.cpp` and press `Ctrl+Shift+B` to build the app.

## Tutorial

See the [wiki](https://github.com/vanjac/chromabrowse/wiki/Tutorial) for an introduction to the app.
