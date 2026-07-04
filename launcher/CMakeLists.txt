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
    src/models/BPValue.cpp
    src/models/AssetRef.cpp
    src/models/AssetRegistry.cpp
    src/models/AssetDependencyScanner.cpp
    src/models/DataTable.cpp
    src/models/AnimationAsset.cpp
    src/editor/EditorWindow.cpp
    src/editor/Viewport2D.cpp
    src/editor/SceneOutliner.cpp
    src/editor/DetailsPanel.cpp
    src/editor/ContentBrowser.cpp
    src/editor/ProjectSettingsDialog.cpp
    src/editor/BlueprintEditor.cpp
    src/editor/BlueprintExecutionContext.cpp
    src/editor/BlueprintVariableScope.cpp
    src/editor/BlueprintNodeExecutor.cpp
    src/editor/BPRuntime.cpp
    src/editor/BattleRuntime.cpp
    src/editor/DataTableRuntimeService.cpp
    src/editor/EffectRuntime.cpp
    src/editor/CardRuntimeController.cpp
    src/editor/BattleUiPresenter.cpp
    src/editor/UiBindingRuntime.cpp
    src/editor/ComponentBPRuntime.cpp
    src/editor/ActorBPRuntime.cpp
    src/editor/LayoutManager.cpp
    src/editor/Recorder.cpp
    src/editor/DocTabBar.cpp
    src/editor/BlueprintFloatWindow.cpp
    src/editor/GameViewport.cpp
    src/models/BPClass.cpp
    src/models/BPMacro.cpp
    src/editor/GlobalVars.cpp
    src/editor/GlobalVarPanel.cpp
    src/editor/EnumEditor.cpp
    src/editor/DataTableEditor.cpp
    src/models/UIDocument.cpp
    src/editor/UIRuntime.cpp
    src/editor/UIEditor.cpp
    src/editor/UISnapGuides.cpp
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
    src/models/BPValue.h
    src/models/AssetRef.h
    src/models/AssetRegistry.h
    src/models/AssetDependencyScanner.h
    src/models/DataTable.h
    src/models/AnimationAsset.h
    src/editor/EditorWindow.h
    src/editor/Viewport2D.h
    src/editor/SceneOutliner.h
    src/editor/DetailsPanel.h
    src/editor/ContentBrowser.h
    src/editor/ProjectSettingsDialog.h
    src/editor/BlueprintEditor.h
    src/editor/BlueprintExecutionContext.h
    src/editor/BlueprintVariableScope.h
    src/editor/BlueprintNodeExecutor.h
    src/editor/UndoCommands.h
    src/editor/BPRuntime.h
    src/editor/BattleRuntime.h
    src/editor/DataTableRuntimeService.h
    src/editor/EffectRuntime.h
    src/editor/CardRuntimeController.h
    src/editor/BattleUiPresenter.h
    src/editor/UiBindingRuntime.h
    src/editor/ComponentBPRuntime.h
    src/editor/ActorBPRuntime.h
    src/editor/BPEval.h
    src/editor/ProjectTags.h
    src/editor/LayoutManager.h
    src/editor/Recorder.h
    src/editor/DocTabBar.h
    src/editor/BlueprintFloatWindow.h
    src/editor/GameViewport.h
    src/models/BPClass.h
    src/models/BPMacro.h
    src/editor/GlobalVars.h
    src/editor/GlobalVarPanel.h
    src/editor/EnumEditor.h
    src/editor/DataTableEditor.h
    src/models/UIDocument.h
    src/editor/UIRuntime.h
    src/editor/UIEditor.h
    src/editor/UISnapGuides.h
)

qt_add_executable(launcher ${SOURCES} ${HEADERS} resources/resources.qrc)

target_include_directories(launcher PRIVATE src)
target_link_libraries(launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets qt6advanceddocking)

set_target_properties(launcher PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
