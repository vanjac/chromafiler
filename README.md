# ChromaFiler

ChromaFiler is an experimental file manager with a unique interface, borrowing ideas from [Spatial file managers](https://en.wikipedia.org/wiki/Spatial_file_manager) and [Miller Column browsers](https://en.wikipedia.org/wiki/Miller_columns).

![Screenshot showing a series of ChromaFiler windows](https://user-images.githubusercontent.com/8228102/192129278-ffdd2e71-8c5c-473f-9985-3f13dd48a8a1.png)

It functions similar to a column-view browser (eg. [Finder](https://en.wikipedia.org/wiki/Finder_(software))), but each column can be broken off into its own window by dragging and dropping. You can use it as a popup menu docked to your taskbar for quickly locating a file, or as a complete replacement for Windows File Explorer (and Notepad).

ChromaFiler works on Windows 8 through 11. Windows 7 support may be added eventually.

## Download

<a href="ms-windows-store://pdp/?productid=XPFFWH44RPBGQJ"><img src="https://getbadgecdn.azureedge.net/images/en-us%20dark.svg" width="135" height="48" alt="Microsoft Store app badge"></a>

Check the [Releases](https://github.com/vanjac/chromafiler/releases) page for the latest beta build. ChromaFiler can also be installed from the [Microsoft Store](https://apps.microsoft.com/store/detail/XPFFWH44RPBGQJ) (single user only). See [installation instructions](https://github.com/vanjac/chromafiler/wiki/Installation) for additional information.

ChromaFiler is still in development and currently does not have automatic update support, so be sure to check back for new releases.

## Tutorial

See the [wiki](https://github.com/vanjac/chromafiler/wiki/Tutorial) for an introduction to the app.

## Building

Building requires the Windows SDK (install version 10.0.19041.0 through the [Visual Studio Installer](https://visualstudio.microsoft.com/downloads/)) and [Visual Studio Code](https://code.visualstudio.com/). The installer is built using [NSIS](https://nsis.sourceforge.io/Main_Page). (You will need to [clone all submodules](https://git-scm.com/book/en/v2/Git-Tools-Submodules#_cloning_submodules).)

VS Code must be launched from the "x64 Native Tools Command Prompt" (search in Start menu) to access the correct MSVC build tools. Type `code` in this prompt to launch VS Code. Open the ChromaFiler directory, then open `src/main.cpp` and press `Ctrl+Shift+B` to build the app in the Debug configuration.

## Suggested pairings

- [Everything](https://www.voidtools.com/) by voidtools (recommend installing with folder context menus)
- [Microsoft PowerToys](https://github.com/microsoft/PowerToys) (see notes on [preview handlers](https://github.com/vanjac/chromafiler/wiki/Installation#preview-handlers))
