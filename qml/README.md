# Aura Cloud Sync — QML Integration Notes

## What was fixed

### 1. Theme singleton import
The original files used `Theme.xxx` without importing it. Every QML file loaded by `StackView` is in a **new context** and must import the module explicitly.

**Fix:** Added `import AuraUI 1.0` to every view file, and updated `qmldir` with the module name:
```
module AuraUI
singleton Theme 1.0 Theme.qml
```

### 2. C++ — register the QML import path
In your `main.cpp`, tell Qt where to find the `AuraUI` module **before** loading any QML:

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    // Point to the folder that contains the qmldir file
    // If QML files are embedded via resources.qrc:
    engine.addImportPath("qrc:/");          // for qrc-embedded files
    // OR if they live next to the binary:
    engine.addImportPath(QCoreApplication::applicationDirPath());

    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
```

### 3. resources.qrc — unchanged format, just ensure all files are listed
All `.qml` files plus `qmldir` must be under the same `qresource prefix="/"` block so `qrc:/` resolves both `main.qml` and the `AuraUI` module directory.

### 4. ListView model — use ListModel instead of JS arrays
`ActivityView` was using a JS array as a `ListView` model with role names that don't work as JS object keys in delegates. Fixed to use `ListModel` with `ListElement` so role names (`fname`, `ftype`, etc.) are accessible directly in delegates.

### 5. Orb rendering
The original orb used a single `Gradient` which rendered as a flat 2-D band rather than a sphere. Replaced with:
- A multi-stop base gradient (vertical) for the sphere depth
- Layered semi-transparent `Rectangle` overlays for teal/purple shimmer and specular highlights
- A pulse `SequentialAnimation on scale` for the breathing effect

## File structure expected
```
your_project/
├── main.cpp
├── your_project.pro   (or CMakeLists.txt)
├── resources.qrc
├── main.qml
├── Theme.qml
├── qmldir             ← must contain: module AuraUI + singleton line
├── DashboardView.qml
├── ActivityView.qml
├── ExplorerView.qml
└── SettingsView.qml
```

## CMake snippet (Qt6)
```cmake
qt_add_qml_module(your_app
    URI AuraUI
    VERSION 1.0
    QML_FILES
        main.qml
        Theme.qml
        DashboardView.qml
        ActivityView.qml
        ExplorerView.qml
        SettingsView.qml
)
```
If using `qt_add_qml_module`, you can remove the manual `qmldir` — Qt generates it automatically.
