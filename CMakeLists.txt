cmake_minimum_required(VERSION 3.21)

set(IPR_RELEASE_VERSION "0.1.0" CACHE STRING "Numeric release version")
project(IPR VERSION "${IPR_RELEASE_VERSION}")
set(IPR_RELEASE_LABEL "0.1.0-alpha" CACHE STRING "Release label (no automatic tag creation)")
set(IPR_DEPLOY_MODE "system" CACHE STRING "system or standalone runtime")
set_property(CACHE IPR_DEPLOY_MODE PROPERTY STRINGS system standalone)
if(NOT IPR_DEPLOY_MODE MATCHES "^(system|standalone)$")
    message(FATAL_ERROR "IPR_DEPLOY_MODE must be system or standalone")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IPR_ENABLE_LIBRAW "Enable CR2/CR3/DNG decoding through LibRaw" ON)
option(IPR_BUILD_RAW_BENCHMARK "Build the command-line RAW loading benchmark" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick Concurrent)
find_package(TIFF REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
qt_standard_project_setup()

set(IPR_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(IPR_INCLUDE_DIRECTORIES
    "${IPR_SOURCE_ROOT}/app"
    "${IPR_SOURCE_ROOT}/cache"
    "${IPR_SOURCE_ROOT}/core"
    "${IPR_SOURCE_ROOT}/editor"
    "${IPR_SOURCE_ROOT}/profiles"
    "${IPR_SOURCE_ROOT}/providers"
    "${IPR_SOURCE_ROOT}/raw"
)

set(IPR_ENGINE_SOURCES
    src/app/UiPreferences.h
    src/app/UiPreferences.cpp
    src/cache/DecodedImageCache.h
    src/cache/DecodedImageCache.cpp
    src/cache/SystemMemory.h
    src/cache/SystemMemory.cpp
    src/cache/ImageCacheController.h
    src/cache/ImageCacheController.cpp
    src/core/ColorPipeline.h
    src/core/ImageMetadata.h
    src/core/ImageImporter.h
    src/core/ImageImporter.cpp
    src/core/WorkingColorSpace.h
    src/core/ColorPipeline.cpp
    src/core/ColorMixer.h
    src/core/ColorMixer.cpp
    src/core/DetailProcessing.h
    src/core/DetailProcessing.cpp
    src/core/ToneRecovery.h
    src/editor/imageeditor.h
    src/editor/imageeditor.cpp
    src/profiles/CameraProfileEngine.h
    src/profiles/CameraProfileEngine.cpp
    src/profiles/PreparedProfileCache.h
    src/profiles/PreparedProfileCache.cpp
    src/providers/imageprovider.h
    src/providers/imageprovider.cpp
    src/providers/ThumbnailProvider.h
    src/providers/ThumbnailProvider.cpp
    src/raw/RawBlackLevelEstimator.h
    src/raw/RawBlackLevelEstimator.cpp
    src/raw/RawDecoder.h
    src/raw/RawDecoder.cpp
    src/raw/RawNormalization.h
    src/raw/RawNormalization.cpp
    src/raw/CameraCharacterization.h
    src/raw/CameraCharacterization.cpp
    src/raw/CameraSignal.h
    src/raw/CameraSignal.cpp
)

set(IPR_QML_FILES
    src/qml/Main.qml
    src/qml/ImageViewport.qml
    src/qml/HistogramModule.qml
    src/qml/Setting.qml
    src/qml/RawDevelopmentModule.qml
    src/qml/SettingsSection.qml
    src/qml/AdjustmentControl.qml
    src/qml/CurveChannelButton.qml
    src/qml/ToneCurveModule.qml
    src/qml/ColorMixerModule.qml
    src/qml/ColorGradingModule.qml
    src/qml/ImageInfoPanel.qml
    src/qml/ThumbnailRibbon.qml
)

# The module keeps its public QML type names and qrc URLs stable while the
# sources live under src/qml.
foreach(qml_file IN LISTS IPR_QML_FILES)
    get_filename_component(qml_alias "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()

add_library(iprEngine STATIC ${IPR_ENGINE_SOURCES})
qt_add_resources(iprEngine ipr_ui_defaults
    PREFIX "/"
    FILES config/ipr-ui.ini config/raw-normalization.json
)
target_include_directories(iprEngine PUBLIC ${IPR_INCLUDE_DIRECTORIES})
target_link_libraries(iprEngine
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Concurrent
        TIFF::TIFF
        ${OpenCV_LIBS}
)

if(IPR_ENABLE_LIBRAW)
    find_path(LIBRAW_INCLUDE_DIR
        NAMES libraw/libraw.h libraw.h
    )
    find_library(LIBRAW_LIBRARY
        NAMES raw libraw
    )

    if(LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
        target_include_directories(iprEngine PUBLIC ${LIBRAW_INCLUDE_DIR})
        target_link_libraries(iprEngine PUBLIC ${LIBRAW_LIBRARY})
        target_compile_definitions(iprEngine PUBLIC IPR_WITH_LIBRAW=1)
        message(STATUS "LibRaw enabled: CR2/CR3/DNG import is available")
    else()
        message(FATAL_ERROR "IPR_ENABLE_LIBRAW=ON requires LibRaw. Install it or explicitly disable RAW support.")
    endif()
endif()

qt_add_executable(appIPR
    MANUAL_FINALIZATION
    src/app/main.cpp
)

qt_add_qml_module(appIPR
    URI IPR
    VERSION 1.0
    QML_FILES ${IPR_QML_FILES}
)

target_link_libraries(appIPR PRIVATE iprEngine)
target_compile_definitions(appIPR PRIVATE IPR_RELEASE_LABEL="${IPR_RELEASE_LABEL}")

# Console tool, intentionally independent of the QML application/sidecar lifecycle.
add_executable(iprGenerateTests src/tools/generate_test_images.cpp)
target_link_libraries(iprGenerateTests PRIVATE iprEngine)
target_compile_definitions(iprGenerateTests PRIVATE IPR_PROJECT_VERSION="${PROJECT_VERSION}")

set_target_properties(appIPR PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER org.ipr.ImageProcessingRoom
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS appIPR
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE)
    set(IPR_TOOL_DESTINATION "appIPR.app/Contents/MacOS")
    set(IPR_DATA_DESTINATION "appIPR.app/Contents/Resources")
else()
    set(IPR_TOOL_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    set(IPR_DATA_DESTINATION "${CMAKE_INSTALL_DATADIR}/IPR")
endif()
install(TARGETS iprGenerateTests RUNTIME DESTINATION "${IPR_TOOL_DESTINATION}")
install(FILES config/ipr-ui.ini config/raw-normalization.json DESTINATION "${IPR_DATA_DESTINATION}/config")
install(FILES packaging/profiles-index.json DESTINATION "${IPR_DATA_DESTINATION}/profiles" RENAME index.json)
install(DIRECTORY DESTINATION "${IPR_DATA_DESTINATION}/profiles/dcp")
install(DIRECTORY DESTINATION "${IPR_DATA_DESTINATION}/profiles/lcp")
install(DIRECTORY licenses/ DESTINATION "${IPR_DATA_DESTINATION}/licenses")
install(FILES packaging/README.md DESTINATION "${IPR_DATA_DESTINATION}")
qt_finalize_executable(appIPR)
include(cmake/Deployment.cmake)

if(IPR_BUILD_RAW_BENCHMARK AND IPR_ENABLE_LIBRAW AND
   LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
    add_executable(rawLoadBenchmark tests/raw_load_benchmark.cpp)
    target_link_libraries(rawLoadBenchmark PRIVATE iprEngine)
endif()

include(CTest)
if(BUILD_TESTING AND IPR_ENABLE_LIBRAW AND LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
    add_executable(editorLifecycleTests tests/editor_lifecycle_test.cpp)
    target_link_libraries(editorLifecycleTests PRIVATE iprEngine)
    add_test(NAME editorLifecycleTests COMMAND editorLifecycleTests)
    set_tests_properties(editorLifecycleTests PROPERTIES TIMEOUT 10 LABELS "unit;pipeline;lifecycle")
    # Manual corpus diagnostic, never part of the default build or CTest.
    add_executable(tonalBaseProbe EXCLUDE_FROM_ALL tests/tonal_base_probe.cpp)
    target_link_libraries(tonalBaseProbe PRIVATE iprEngine)
    add_executable(rawNormalizationCorpusProbe EXCLUDE_FROM_ALL tests/raw_normalization_corpus_probe.cpp)
    target_link_libraries(rawNormalizationCorpusProbe PRIVATE iprEngine)
    add_executable(openingLatencyProbe EXCLUDE_FROM_ALL tests/opening_latency_probe.cpp)
    target_link_libraries(openingLatencyProbe PRIVATE iprEngine)
    add_executable(wbCameraProbe EXCLUDE_FROM_ALL tests/wb_camera_probe.cpp)
    target_link_libraries(wbCameraProbe PRIVATE iprEngine)
    add_executable(rawOpeningProbe EXCLUDE_FROM_ALL tests/raw_opening_probe.cpp)
    target_link_libraries(rawOpeningProbe PRIVATE iprEngine)
    add_executable(rawOpeningTrials EXCLUDE_FROM_ALL tests/raw_opening_trials.cpp)
    target_link_libraries(rawOpeningTrials PRIVATE iprEngine)
    add_executable(rawOpeningSplineTests tests/raw_opening_trials.cpp)
    target_link_libraries(rawOpeningSplineTests PRIVATE iprEngine)
    add_test(NAME rawOpeningSplineTests COMMAND rawOpeningSplineTests --self-test)
    set_tests_properties(rawOpeningSplineTests PROPERTIES LABELS "unit;raw;diagnostic")
    add_executable(rawOpeningProbeTests tests/raw_opening_probe_test.cpp)
    target_link_libraries(rawOpeningProbeTests PRIVATE iprEngine)
    add_test(NAME rawOpeningProbeTests COMMAND rawOpeningProbeTests)
    set_tests_properties(rawOpeningProbeTests PROPERTIES LABELS "unit;raw;diagnostic")
endif()
if(BUILD_TESTING)
    # Pure numerical experiment: no image corpus, Qt plugin or production renderer.
    add_executable(rawHighlightHeadroomTests tests/raw_highlight_headroom_test.cpp)
    target_compile_features(rawHighlightHeadroomTests PRIVATE cxx_std_17)
    add_test(NAME rawHighlightHeadroomTests COMMAND rawHighlightHeadroomTests)
    set_tests_properties(rawHighlightHeadroomTests PROPERTIES LABELS "unit;raw;diagnostic" TIMEOUT 10)
    function(ipr_add_test target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE iprEngine)
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    ipr_add_test(colorPipelineTests tests/color_pipeline_test.cpp)
    ipr_add_test(rec2020PipelineTests tests/rec2020_pipeline_test.cpp)
    set_tests_properties(rec2020PipelineTests PROPERTIES LABELS "unit;pipeline;colour" TIMEOUT 60)
    ipr_add_test(detailProcessingTests tests/detail_processing_test.cpp)
    ipr_add_test(imageExportTests tests/image_export_test.cpp)
    ipr_add_test(toneRecoveryTests tests/tone_recovery_test.cpp)
    set_tests_properties(toneRecoveryTests PROPERTIES LABELS "unit;pipeline;lighting")
    add_executable(testImageGeneratorTests tests/test_image_generator_test.cpp)
    target_link_libraries(testImageGeneratorTests PRIVATE iprEngine)
    add_dependencies(testImageGeneratorTests iprGenerateTests)
    add_test(NAME testImageGeneratorTests
        COMMAND testImageGeneratorTests $<TARGET_FILE:iprGenerateTests>)
    set_tests_properties(testImageGeneratorTests PROPERTIES
        LABELS "unit;export;generator" TIMEOUT 120)
    ipr_add_test(colorMixerMathTests tests/color_mixer_math_test.cpp)
    add_executable(colorMixerNeutralProbe tests/color_mixer_neutral_probe.cpp)
    target_link_libraries(colorMixerNeutralProbe PRIVATE iprEngine)
    add_test(NAME colorMixerNeutralTests COMMAND colorMixerNeutralProbe)
    ipr_add_test(colorMixerQmlTests tests/color_mixer_qml_test.cpp)
    target_compile_definitions(colorMixerQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(colorMixerQmlTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(settingsQmlTests tests/settings_qml_test.cpp)
    target_compile_definitions(settingsQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(settingsQmlTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(imageViewportQmlTests tests/image_viewport_qml_test.cpp)
    target_compile_definitions(imageViewportQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(imageViewportQmlTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
        LABELS "unit;viewport;qml")
    ipr_add_test(uiPreferencesTests tests/ui_preferences_test.cpp)
    ipr_add_test(uiPreferencesQmlTests tests/ui_preferences_qml_test.cpp)
    qt_add_resources(uiPreferencesQmlTests ipr_ui_test_qml
        PREFIX "/IPR"
        FILES ${IPR_QML_FILES}
    )
    target_compile_definitions(uiPreferencesQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(uiPreferencesTests uiPreferencesQmlTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(rawDecoderTests tests/raw_decoder_test.cpp)
    ipr_add_test(imageImportTests tests/image_import_test.cpp)
    set_tests_properties(imageImportTests PROPERTIES LABELS "unit;import;pipeline")
    ipr_add_test(rawNormalizationTests tests/raw_normalization_test.cpp)
    ipr_add_test(editorRawDevelopmentTests tests/editor_raw_development_test.cpp)
    set_tests_properties(editorRawDevelopmentTests PROPERTIES LABELS "unit;raw;settings" TIMEOUT 60)
    ipr_add_test(rawBlackLevelEstimatorTests tests/raw_black_level_estimator_test.cpp)
    ipr_add_test(cameraProfileEngineTests tests/camera_profile_engine_test.cpp)
    ipr_add_test(preparedProfileCacheTests tests/prepared_profile_cache_test.cpp)
    ipr_add_test(imageCacheControllerTests tests/image_cache_controller_test.cpp)
    ipr_add_test(editorImageCacheTests tests/editor_image_cache_test.cpp)
    set_tests_properties(editorImageCacheTests PROPERTIES LABELS "unit;cache;memory;editor;prefetch" TIMEOUT 30)
    # Paced by the controller's 500 ms sampling; macOS runners coalesce timers.
    set_tests_properties(imageCacheControllerTests PROPERTIES LABELS "unit;cache;memory;thread" TIMEOUT 60)
    ipr_add_test(thumbnailProviderTests tests/thumbnail_provider_test.cpp)
    set_tests_properties(thumbnailProviderTests PROPERTIES LABELS "unit;thumbnail;thread" TIMEOUT 20)
    set_tests_properties(preparedProfileCacheTests PROPERTIES LABELS "unit;profiles;cache" TIMEOUT 30)
    set_tests_properties(colorPipelineTests PROPERTIES LABELS "unit;pipeline")
    set_tests_properties(detailProcessingTests PROPERTIES LABELS "unit;pipeline;detail;noise")
    set_tests_properties(imageExportTests PROPERTIES LABELS "unit;export;pipeline")
    set_tests_properties(colorMixerMathTests colorMixerNeutralTests colorMixerQmlTests
        PROPERTIES LABELS "unit;color-mixer")
    set_tests_properties(settingsQmlTests PROPERTIES LABELS "unit;settings;qml")
    set_tests_properties(uiPreferencesTests uiPreferencesQmlTests
        PROPERTIES LABELS "unit;preferences;qml")
    set_tests_properties(rawDecoderTests rawBlackLevelEstimatorTests rawNormalizationTests
        PROPERTIES LABELS "unit;raw")
    set_tests_properties(cameraProfileEngineTests PROPERTIES LABELS "unit;profiles")
endif()

if(BUILD_TESTING AND UNIX AND IPR_ENABLE_LIBRAW AND
   LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
    add_executable(rawProfileIntegrationProbe tests/raw_profile_integration_probe.cpp)
    target_link_libraries(rawProfileIntegrationProbe PRIVATE iprEngine)

    add_executable(whiteBalanceEstimationTest tests/white_balance_estimation_test.cpp)
    target_link_libraries(whiteBalanceEstimationTest PRIVATE iprEngine)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/image/6V1A9933.CR3")
        add_test(
            NAME whiteBalanceEstimationTest
            COMMAND whiteBalanceEstimationTest
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/image/6V1A9933.CR3"
        )
    endif()

    add_executable(rawBlackLevelCorpusProbe tests/raw_black_level_corpus_probe.cpp)
    target_link_libraries(rawBlackLevelCorpusProbe PRIVATE iprEngine)

    add_executable(tonalReferenceComparisonProbe
        tests/tonal_reference_comparison_probe.cpp
    )
    target_link_libraries(tonalReferenceComparisonProbe PRIVATE iprEngine)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/image/6V1A9933.CR3" AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/image/6V1A9933.jpg")
        add_test(
            NAME tonalReferenceComparisonProbe
            COMMAND tonalReferenceComparisonProbe
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/image/6V1A9933.CR3"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/image/6V1A9933.jpg"
                "${CMAKE_CURRENT_BINARY_DIR}/6V1A9933-auto-wb.jpg"
                "${CMAKE_CURRENT_BINARY_DIR}/6V1A9933-camera-wb.jpg"
        )
        set_tests_properties(tonalReferenceComparisonProbe PROPERTIES TIMEOUT 180)
    endif()
endif()
