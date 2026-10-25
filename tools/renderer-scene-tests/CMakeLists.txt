cmake_minimum_required(VERSION 3.15)
project(RendererSceneTests LANGUAGES CXX)
enable_testing()
add_executable(renderer_scene_tests
    scene_tests.cpp
    view_tests.cpp
    model_pose_tests.cpp
    model_scene_tests.cpp
    light_tests.cpp
    image_tests.cpp
    procedural_tests.cpp
    terrain_tests.cpp
    fog_tests.cpp
    feedback_tests.cpp
    frame_tests.cpp
    frame_storage_tests.cpp
    event_tests.cpp
    effect_tests.cpp
    game_visual_tests.cpp
    electrical_tests.cpp
    canvas_tests.cpp
    module_context_tests.cpp
    module_mutation_tests.cpp
    sky_weather_tests.cpp
    display_label_tests.cpp
    view_deformation_tests.cpp
    auxiliary_view_tests.cpp
    room_corona_tests.cpp
    raster_trail_tests.cpp
    save_contract_tests.cpp
    display_output_tests.cpp
    automap_tests.cpp
    ../../Descent3/SceneCanvas.cpp
    ../../Descent3/SceneModuleContext.cpp
    ../../Descent3/SceneAutomapGeometry.cpp
    ../../renderer/SceneLighting.cpp
    ../../renderer/SceneGeometry.cpp
    ../../renderer/ScenePortals.cpp
    ../../renderer/SceneImages.cpp
    ../../renderer/SceneTerrain.cpp
    ../../renderer/SceneFog.cpp
    ../../model/ModelPose.cpp
    ../../model/SceneModelAsset.cpp
    ../../vecmat/vector.cpp
    ../../fix/fix.cpp
    ../../renderer/Scene.cpp
    ../../renderer/SceneView.cpp
    ../../Descent3/SceneExtraction.cpp)
target_sources(renderer_scene_tests PRIVATE ../../Descent3/SceneObservations.cpp)
target_sources(renderer_scene_tests PRIVATE ../../Descent3/SceneFrame.cpp)
target_sources(renderer_scene_tests PRIVATE ../../Descent3/GameVisualEffects.cpp)
target_sources(renderer_scene_tests PRIVATE ../../Descent3/GameElectricalWeapons.cpp)
target_sources(renderer_scene_tests PRIVATE ../../Descent3/GameSkyWeather.cpp)
target_sources(renderer_scene_tests PRIVATE ../../Descent3/gameevent.cpp)
target_compile_features(renderer_scene_tests PRIVATE cxx_std_17)
target_include_directories(renderer_scene_tests PRIVATE ../../lib ../../Descent3)
# Standalone clean checkouts have no game-build-generated version header.
# Generate their own copy in the build tree; never depend on an ignored source
# header or stamp a hand-written fake game version into the contract fixture.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(renderer_scene_test_revision
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_SOURCE_DIR}/../UpdateRevision.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/gitinfo.h"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/gitinfo.h")
add_dependencies(renderer_scene_tests renderer_scene_test_revision)
target_include_directories(renderer_scene_tests BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(MSVC)
    target_compile_options(renderer_scene_tests PRIVATE /W4 /EHsc)
    target_compile_definitions(renderer_scene_tests PRIVATE WIN32 IS_WINDOWS)
endif()
add_test(NAME renderer_scene_contract COMMAND renderer_scene_tests)

add_executable(renderer_args_tests args_tests.cpp ../../Descent3/args.cpp)
target_compile_features(renderer_args_tests PRIVATE cxx_std_17)
target_include_directories(renderer_args_tests PRIVATE ../../lib ../../Descent3)
if(MSVC)
    target_compile_definitions(renderer_args_tests PRIVATE WIN32 IS_WINDOWS)
elseif(UNIX)
    target_compile_definitions(renderer_args_tests PRIVATE __LINUX__)
endif()
add_test(NAME renderer_argument_contract COMMAND renderer_args_tests)
if(MSVC)
    # Exercise the GNU option spelling branch on the Windows test host too.
    add_executable(renderer_args_gnu_tests args_tests.cpp ../../Descent3/args.cpp)
    target_compile_features(renderer_args_gnu_tests PRIVATE cxx_std_17)
    target_include_directories(renderer_args_gnu_tests PRIVATE ../../lib ../../Descent3)
    target_compile_definitions(renderer_args_gnu_tests PRIVATE WIN32 IS_WINDOWS __LINUX__ strcasecmp=_stricmp)
    add_test(NAME renderer_argument_gnu_contract COMMAND renderer_args_gnu_tests)
endif()
