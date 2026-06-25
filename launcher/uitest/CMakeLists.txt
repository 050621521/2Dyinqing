cmake_minimum_required(VERSION 3.20)
project(uinode_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_executable(uinode_test
    test_ui_nodes.cpp
    ${SRC_ROOT}/models/LevelDocument.cpp
    ${SRC_ROOT}/models/UIDocument.cpp
    ${SRC_ROOT}/models/BPMacro.cpp
    ${SRC_ROOT}/editor/BPRuntime.cpp
    ${SRC_ROOT}/editor/UIRuntime.cpp
)
target_include_directories(uinode_test PRIVATE ${SRC_ROOT})
target_link_libraries(uinode_test PRIVATE Qt6::Core Qt6::Gui)

add_executable(render_progress_test
    render_progress_test.cpp
    ${SRC_ROOT}/models/LevelDocument.cpp
    ${SRC_ROOT}/models/UIDocument.cpp
    ${SRC_ROOT}/models/AnimationAsset.cpp
    ${SRC_ROOT}/editor/UIRuntime.cpp
    ${SRC_ROOT}/editor/GameViewport.cpp
)
target_include_directories(render_progress_test PRIVATE ${SRC_ROOT})
target_link_libraries(render_progress_test PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

add_executable(test_real_project
    test_real_project.cpp
    ${SRC_ROOT}/models/LevelDocument.cpp
    ${SRC_ROOT}/models/UIDocument.cpp
    ${SRC_ROOT}/models/BPMacro.cpp
    ${SRC_ROOT}/editor/BPRuntime.cpp
    ${SRC_ROOT}/editor/UIRuntime.cpp
)
target_include_directories(test_real_project PRIVATE ${SRC_ROOT})
target_link_libraries(test_real_project PRIVATE Qt6::Core Qt6::Gui)

add_executable(follow_test
    follow_test.cpp
    ${SRC_ROOT}/models/LevelDocument.cpp
    ${SRC_ROOT}/models/UIDocument.cpp
    ${SRC_ROOT}/models/BPMacro.cpp
    ${SRC_ROOT}/models/AnimationAsset.cpp
    ${SRC_ROOT}/models/BPValue.cpp
    ${SRC_ROOT}/editor/BPRuntime.cpp
    ${SRC_ROOT}/editor/UIRuntime.cpp
)
target_include_directories(follow_test PRIVATE ${SRC_ROOT})
target_link_libraries(follow_test PRIVATE Qt6::Core Qt6::Gui)
