cmake_minimum_required(VERSION 3.15)
project(RendererSceneTests LANGUAGES CXX)
enable_testing()
add_executable(renderer_scene_tests
    scene_tests.cpp
    view_tests.cpp
    ../../renderer/Scene.cpp
    ../../renderer/SceneView.cpp
    ../../Descent3/SceneExtraction.cpp)
target_compile_features(renderer_scene_tests PRIVATE cxx_std_17)
target_include_directories(renderer_scene_tests PRIVATE ../../lib ../../Descent3)
if(MSVC)
    target_compile_options(renderer_scene_tests PRIVATE /W4 /EHsc)
    target_compile_definitions(renderer_scene_tests PRIVATE WIN32 IS_WINDOWS)
endif()
add_test(NAME renderer_scene_contract COMMAND renderer_scene_tests)
