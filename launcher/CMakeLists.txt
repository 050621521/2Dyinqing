cmake_minimum_required(VERSION 3.20)
project(2DYinqingLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

include(FetchContent)
FetchContent_Declare(
    QtADS
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        4.3.1
)
set(ADS_VERSION "4.3.1" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QtADS)

set(SOURCES
    src/main.cpp
    src/launcher/LauncherWindow.cpp
    src/launcher/CategoryList.cpp
    src/launcher/ProjectGrid.cpp
    src/launcher/ProjectCard.cpp
    src/launcher/NewProjectDialog.cpp
    src/launcher/SearchBar.cpp
    src/models/ProjectManager.cpp
    src/models/LevelDocument.cpp
    src/editor/EditorWindow.cpp
    src/editor/Viewport2D.cpp
    src/editor/SceneOutliner.cpp
    src/editor/DetailsPanel.cpp
    src/editor/ContentBrowser.cpp
    src/editor/ProjectSettingsDialog.cpp
    src/editor/BlueprintEditor.cpp
    src/editor/BPRuntime.cpp
    src/editor/ActorBPRuntime.cpp
    src/editor/LayoutManager.cpp
    src/editor/DocTabBar.cpp
    src/editor/BlueprintFloatWindow.cpp
    src/editor/GameViewport.cpp
    src/models/BPClass.cpp
)

set(HEADERS
    src/launcher/LauncherWindow.h
    src/launcher/CategoryList.h
    src/launcher/ProjectGrid.h
    src/launcher/ProjectCard.h
    src/launcher/NewProjectDialog.h
    src/launcher/SearchBar.h
    src/models/ProjectInfo.h
    src/models/ProjectManager.h
    src/models/ActorTypeUtils.h
    src/editor/EditorWindow.h
    src/editor/Viewport2D.h
    src/editor/SceneOutliner.h
    src/editor/DetailsPanel.h
    src/editor/ContentBrowser.h
    src/editor/ProjectSettingsDialog.h
    src/editor/BlueprintEditor.h
    src/editor/BPRuntime.h
    src/editor/ActorBPRuntime.h
    src/editor/LayoutManager.h
    src/editor/DocTabBar.h
    src/editor/BlueprintFloatWindow.h
    src/editor/GameViewport.h
    src/models/BPClass.h
)

qt_add_executable(launcher ${SOURCES} ${HEADERS} resources/resources.qrc)

target_include_directories(launcher PRIVATE src)
target_link_libraries(launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets qt6advanceddocking)

set_target_properties(launcher PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
