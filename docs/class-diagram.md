# Class diagram

```mermaid
classDiagram
  IUnknownImpl <|-- ItemWindow
  ItemWindow <|-- FolderWindow
  FolderWindow <|-- TrayWindow
  ItemWindow <|-- ThumbnailWindow
  ItemWindow <|-- PreviewWindow
  ItemWindow <|-- TextWindow

  IUnknownImpl <|-- StoppableThread

  class IUnknownImpl {
    -long refCount
    (impl IUnknown)
  }
  class ItemWindow {
    <<abstract>>
    IShellItem *item
    +ItemWindow(parent, item)
    +create(rect, showCommand) bool
    +requestedSize() SIZE
    (impl IDropSource)
    (impl IDropTarget)
  }
  class FolderWindow {
    IExplorerBrowser *browser
    (impl IServiceProvider)
    (impl ICommDlgBrowser)
    (impl IExplorerBrowserEvents)
  }
  class PreviewWindow {
    PreviewWindow(parent, item, CLSID)
    IPreviewHandler *preview
    (impl IPreviewHandlerFrame)
  }
  class StoppableThread {
    +start()
    +stop()
    #run()
    #isStopped()
  }
```
