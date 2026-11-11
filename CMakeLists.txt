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
# Analysis tools (test image generator, in-memory render server): development
# only. The production profile builds IPR with them and the tests OFF.
option(IPR_BUILD_TOOLS "Build the analysis tools of src/tools" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Concurrent)
find_package(TIFF REQUIRED)
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
    "${IPR_SOURCE_ROOT}/stack"
    "${IPR_SOURCE_ROOT}/detect"
    "${IPR_SOURCE_ROOT}/panorama"
    "${IPR_SOURCE_ROOT}/tasks"
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
    src/cache/RenderStageCache.h
    src/cache/RenderStageCache.cpp
    src/tasks/TaskRunner.h
    src/tasks/TaskRunner.cpp
    src/core/ColorPipeline.h
    src/core/ImageMetadata.h
    src/core/ImageImporter.h
    src/core/ImageImporter.cpp
    src/core/ImageExporter.h
    src/core/ImageExporter.cpp
    src/core/EditRecipe.h
    src/core/Exposure.h
    src/core/Exposure.cpp
    src/core/LocalAdjustments.h
    src/core/LocalAdjustments.cpp
    src/core/LocalValues.h
    src/core/LocalValues.cpp
    src/core/RecipeCodec.h
    src/core/RecipeCodec.cpp
    src/core/EditHistory.h
    src/core/EditHistory.cpp
    src/core/Sidecar.h
    src/core/Sidecar.cpp
    src/core/WorkingColorSpace.h
    src/core/ImageMath.h
    src/core/ColorPipeline.cpp
    src/core/ColorMixer.h
    src/core/ColorMixer.cpp
    src/core/ColorMixerModel.h
    src/core/PointColor.h
    src/core/PointColor.cpp
    src/core/DetailProcessing.h
    src/core/DetailProcessing.cpp
    src/core/Geometry.h
    src/core/Geometry.cpp
    src/core/Dehaze.h
    src/core/Dehaze.cpp
    src/core/Grain.h
    src/core/Grain.cpp
    src/core/LocalContrast.h
    src/core/LocalContrast.cpp
    src/core/ColorAdjustments.h
    src/core/ColorAdjustments.cpp
    src/core/ColorGrading.h
    src/core/ColorGrading.cpp
    src/detect/LineDetection.h
    src/detect/LineDetection.cpp
    src/detect/Straightening.h
    src/detect/Straightening.cpp
    src/stack/FrameSpool.h
    src/stack/FrameSpool.cpp
    src/core/Retouch.h
    src/core/Retouch.cpp
    src/panorama/PanoramaFeatures.h
    src/panorama/PanoramaFeatures.cpp
    src/panorama/PanoramaGeometry.h
    src/panorama/PanoramaGeometry.cpp
    src/panorama/PanoramaBlend.h
    src/panorama/PanoramaBlend.cpp
    src/panorama/PanoramaFill.h
    src/panorama/PanoramaFill.cpp
    src/panorama/PanoramaJob.h
    src/panorama/PanoramaJob.cpp
    src/stack/StackAlignment.h
    src/stack/StackAlignment.cpp
    src/stack/StackMerge.h
    src/stack/StackMerge.cpp
    src/stack/StackTurbulence.h
    src/stack/StackTurbulence.cpp
    src/stack/StackJob.h
    src/stack/StackJob.cpp
    src/stack/StackSession.h
    src/stack/StackSession.cpp
    src/raw/CameraDng.h
    src/raw/CameraDng.cpp
    src/core/PerceptualColour.h
    src/core/PerceptualColour.cpp
    src/core/Contrast.h
    src/core/Contrast.cpp
    src/core/Histogram.h
    src/core/Histogram.cpp
    src/core/ToneCurve.h
    src/core/ToneCurve.cpp
    src/core/ToneRecovery.h
    src/core/ToneRecovery.cpp
    src/core/ToneRecoveryHighlights.h
    src/core/ToneRecoveryModel.h
    src/core/ToneStatistics.h
    src/core/ToneStatistics.cpp
    src/core/Vibrance.h
    src/core/Vibrance.cpp
    src/core/VibranceModel.h
    src/editor/imageeditor.h
    src/editor/imageeditor.cpp
    src/editor/AdjustmentTable.h
    src/editor/AdjustmentTable.cpp
    src/editor/RenderChain.h
    src/editor/RenderChain.cpp
    src/profiles/CameraProfileEngine.h
    src/profiles/CameraProfileEngine.cpp
    src/profiles/PreparedProfileCache.h
    src/profiles/PreparedProfileCache.cpp
    src/providers/imageprovider.h
    src/providers/imageprovider.cpp
    src/providers/ThumbnailProvider.h
    src/providers/ThumbnailProvider.cpp
    src/providers/GradingWheelProvider.h
    src/providers/GradingWheelProvider.cpp
    src/raw/Demosaic.h
    src/raw/Demosaic.cpp
    src/raw/RawBlackLevelEstimator.h
    src/raw/RawBlackLevelEstimator.cpp
    src/raw/RawDecoder.h
    src/raw/RawDecoder.cpp
    src/raw/RawInput.h
    src/raw/RawInput.cpp
    src/raw/LibRawInput.h
    src/raw/LibRawInput.cpp
    src/raw/RawDevelopment.h
    src/raw/RawDevelopment.cpp
    src/raw/CameraDenoise.h
    src/raw/CameraDenoise.cpp
    src/raw/RawNormalization.h
    src/raw/RawNormalization.cpp
    src/raw/CameraCharacterization.h
    src/raw/CameraCharacterization.cpp
    src/raw/NoiseLaw.h
    src/raw/NoiseLaw.cpp
    src/raw/CfaNoiseStage.h
    src/raw/CfaNoiseStage.cpp
    src/raw/CameraSignal.h
    src/raw/CameraSignal.cpp
)

set(IPR_QML_FILES
    src/qml/Main.qml
    src/qml/ApplicationActions.qml
    src/qml/PhotoContextMenu.qml
    src/qml/ActionMenuItem.qml
    src/qml/ImageViewport.qml
    src/qml/HistogramModule.qml
    src/qml/Setting.qml
    src/qml/RawDevelopmentModule.qml
    src/qml/BackendCheckBox.qml
    src/qml/OpticsModule.qml
    src/qml/FilmstripNavigation.qml
    src/qml/ChevronIcon.qml
        src/qml/ProcessingStatus.qml
        src/qml/PanelResizeHandle.qml
        src/qml/ExportReviewDialog.qml
        src/qml/ConfirmationFooter.qml
    src/qml/RawCalibrationModule.qml
    src/qml/PreferencesDialog.qml
    src/qml/PreferenceButton.qml
    src/qml/CommandErrorPopup.qml
    src/qml/PreferenceCheckBox.qml
    src/qml/PreferenceSpinBox.qml
    src/qml/PreferenceComboBox.qml
    src/qml/PreferenceScrollView.qml
    src/qml/InterfacePreferencesPane.qml
    src/qml/AppearancePreferencesPane.qml
    src/qml/ImageBackgroundOptions.qml
    src/qml/ImageCanvasBackground.qml
    src/qml/AdvancedRawPreferencesPane.qml
    src/qml/InterfacePaletteSelector.qml
    src/qml/AppMenu.qml
    src/qml/HistoryModule.qml
    src/qml/GeometryPanel.qml
    src/qml/AssemblePanel.qml
    src/qml/RetouchPanel.qml
    src/qml/MasksPanel.qml
    src/qml/MaskOverlay.qml
    src/qml/CropOverlay.qml
    src/qml/PanelTabBar.qml
    src/qml/PanelScrollBar.qml
    src/qml/SettingsSection.qml
    src/qml/AdjustmentControl.qml
    src/qml/CurveChannelButton.qml
    src/qml/ToneCurveModule.qml
    src/qml/ColorMixerModule.qml
    src/qml/ColorGradingModule.qml
    src/qml/PointColorModule.qml
    src/qml/SectionNote.qml
    src/qml/ImageInfoPanel.qml
    src/qml/ThumbnailRibbon.qml
)

# The module keeps its public QML type names and qrc URLs stable while the
# sources live under src/qml.
foreach(qml_file IN LISTS IPR_QML_FILES)
    get_filename_component(qml_alias "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_alias}")
endforeach()

# The image layer: standard C++ only, no Qt (roadmap-couche-image-sans-opencv.md).
find_package(Threads REQUIRED)
add_library(iprImage STATIC src/image/Types.h src/image/Image.h src/image/Image.cpp
    src/image/Parallel.h src/image/Parallel.cpp src/image/Filters.h src/image/Filters.cpp
    src/image/Resize.h src/image/Resize.cpp src/image/Warp.h src/image/Warp.cpp src/image/Arrange.h src/image/Arrange.cpp
    src/image/Neighbourhood.h src/image/Neighbourhood.cpp src/image/Pixels.h src/image/Pixels.cpp
    src/image/Analysis.h src/image/Analysis.cpp src/image/Random.h)
target_include_directories(iprImage PUBLIC src/image)
target_link_libraries(iprImage PUBLIC Threads::Threads)
set_target_properties(iprImage PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Optional neural RAW denoising (preferences, off by default): the module always builds and
# reports itself unavailable without IPR_ENABLE_ONNX_RUNTIME; see src/neural/README.md.
add_subdirectory(src/neural)

add_library(iprEngine STATIC ${IPR_ENGINE_SOURCES})
qt_add_resources(iprEngine ipr_ui_defaults
    PREFIX "/"
    FILES config/ipr-ui.ini config/raw-normalization.json config/noise-laws.json
)
target_include_directories(iprEngine PUBLIC ${IPR_INCLUDE_DIRECTORIES})
target_link_libraries(iprEngine
    PUBLIC
        iprImage
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::Concurrent
        TIFF::TIFF
        iprNeural
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
if(WIN32 AND IPR_ENABLE_ONNX_RUNTIME)
    # The runtime DLLs beside the executable for development runs; install() deploys them in packages.
    foreach(runtime IN LISTS IPR_NEURAL_RUNTIME_FILES)
        add_custom_command(TARGET appIPR POST_BUILD COMMAND ${CMAKE_COMMAND}
            -E copy_if_different "${runtime}" "$<TARGET_FILE_DIR:appIPR>")
    endforeach()
endif()
target_compile_definitions(appIPR PRIVATE IPR_RELEASE_LABEL="${IPR_RELEASE_LABEL}")

# Console tools, intentionally independent of the QML application/sidecar
# lifecycle: TIFF exports for visual review and in-memory renders for analysis,
# from the same test recipes.
if(IPR_BUILD_TOOLS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tools/TestRecipes.cpp")
        message(FATAL_ERROR "IPR_BUILD_TOOLS=ON needs src/tools, absent from this tree (main carries production code only): configure with -DIPR_BUILD_TOOLS=OFF")
    endif()
    add_library(iprTestRecipes STATIC src/tools/TestRecipes.cpp)
    target_include_directories(iprTestRecipes PUBLIC src/tools)
    target_link_libraries(iprTestRecipes PUBLIC iprEngine)
    add_executable(iprGenerateTests src/tools/generate_test_images.cpp src/tools/ParallelJobs.cpp)
    target_link_libraries(iprGenerateTests PRIVATE iprTestRecipes)
    add_executable(iprRenderServer src/tools/render_server.cpp)
    target_link_libraries(iprRenderServer PRIVATE iprTestRecipes)
    add_executable(iprSidecarDump src/tools/sidecar_dump.cpp src/tools/SidecarJson.cpp)
    target_link_libraries(iprSidecarDump PRIVATE iprEngine)
    add_executable(iprSidecarAudit src/tools/sidecar_audit.cpp)
    target_link_libraries(iprSidecarAudit PRIVATE iprEngine)
endif()

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
if(IPR_BUILD_TOOLS)
    install(TARGETS iprGenerateTests RUNTIME DESTINATION "${IPR_TOOL_DESTINATION}")
endif()
install(FILES config/ipr-ui.ini config/raw-normalization.json config/noise-laws.json DESTINATION "${IPR_DATA_DESTINATION}/config")
install(FILES packaging/profiles-index.json DESTINATION "${IPR_DATA_DESTINATION}/profiles" RENAME index.json)
install(DIRECTORY DESTINATION "${IPR_DATA_DESTINATION}/profiles/dcp")
install(DIRECTORY DESTINATION "${IPR_DATA_DESTINATION}/profiles/lcp")
install(DIRECTORY licenses/ DESTINATION "${IPR_DATA_DESTINATION}/licenses")
install(FILES LICENSE DESTINATION "${IPR_DATA_DESTINATION}/licenses" RENAME IPR-AGPL-3.0.txt)
install(FILES packaging/README.md DESTINATION "${IPR_DATA_DESTINATION}")
qt_finalize_executable(appIPR)
include(cmake/Deployment.cmake)

if(IPR_BUILD_RAW_BENCHMARK AND IPR_ENABLE_LIBRAW AND
   LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
    add_executable(rawLoadBenchmark tests/raw_load_benchmark.cpp)
    target_link_libraries(rawLoadBenchmark PRIVATE iprEngine)
endif()

include(CTest)
# The tests exercise the tools and live under tests/: both must be present.
if(BUILD_TESTING AND NOT IPR_BUILD_TOOLS)
    message(FATAL_ERROR "BUILD_TESTING=ON needs IPR_BUILD_TOOLS=ON (the tests use the analysis tools)")
endif()
if(BUILD_TESTING AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    message(FATAL_ERROR "BUILD_TESTING=ON needs tests/, absent from this tree (main carries production code only): configure with -DBUILD_TESTING=OFF")
endif()
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
    # Demosaicing study: LibRaw algorithms and IPR's RCD on the same RAW (manual, not in CTest).
    add_executable(demosaicProbe EXCLUDE_FROM_ALL tests/demosaic_probe.cpp)
    target_link_libraries(demosaicProbe PRIVATE iprEngine)
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
    ipr_add_test(renderStageCacheTests tests/render_stage_cache_test.cpp)
    set_tests_properties(renderStageCacheTests PROPERTIES LABELS "unit;pipeline;cache" TIMEOUT 120)
    ipr_add_test(vibranceMathTests tests/vibrance_math_test.cpp)
    set_tests_properties(vibranceMathTests PROPERTIES LABELS "unit;pipeline;colour")
    # The image layer without Qt: only the standard library.
    add_executable(imageCoreTests tests/image_core_test.cpp)
    target_link_libraries(imageCoreTests PRIVATE iprImage)
    add_test(NAME imageCoreTests COMMAND imageCoreTests)
    set_tests_properties(imageCoreTests PROPERTIES LABELS "unit;image")
    add_executable(imageFiltersTests tests/image_filters_test.cpp)
    target_link_libraries(imageFiltersTests PRIVATE iprImage)
    add_test(NAME imageFiltersTests COMMAND imageFiltersTests)
    set_tests_properties(imageFiltersTests PROPERTIES LABELS "unit;image")
    add_executable(iprExportModuleProbe EXCLUDE_FROM_ALL tests/export_module_probe.cpp)
    target_link_libraries(iprExportModuleProbe PRIVATE iprEngine)
    add_executable(imageResizeTests tests/image_resize_test.cpp)
    target_link_libraries(imageResizeTests PRIVATE iprImage)
    add_test(NAME imageResizeTests COMMAND imageResizeTests)
    set_tests_properties(imageResizeTests PROPERTIES LABELS "unit;image")
    add_executable(imageArrangeTests tests/image_arrange_test.cpp)
    target_link_libraries(imageArrangeTests PRIVATE iprImage)
    add_test(NAME imageArrangeTests COMMAND imageArrangeTests)
    set_tests_properties(imageArrangeTests PROPERTIES LABELS "unit;image")
    add_executable(imageWarpTests tests/image_warp_test.cpp)
    target_link_libraries(imageWarpTests PRIVATE iprImage)
    add_test(NAME imageWarpTests COMMAND imageWarpTests)
    set_tests_properties(imageWarpTests PROPERTIES LABELS "unit;image")
    add_executable(imageNeighbourhoodTests tests/image_neighbourhood_test.cpp)
    target_link_libraries(imageNeighbourhoodTests PRIVATE iprImage)
    add_test(NAME imageNeighbourhoodTests COMMAND imageNeighbourhoodTests)
    set_tests_properties(imageNeighbourhoodTests PROPERTIES LABELS "unit;image")
    add_executable(imagePixelsTests tests/image_pixels_test.cpp)
    target_link_libraries(imagePixelsTests PRIVATE iprImage)
    add_test(NAME imagePixelsTests COMMAND imagePixelsTests)
    set_tests_properties(imagePixelsTests PROPERTIES LABELS "unit;image")
    add_executable(imageAnalysisTests tests/image_analysis_test.cpp)
    target_link_libraries(imageAnalysisTests PRIVATE iprImage)
    add_test(NAME imageAnalysisTests COMMAND imageAnalysisTests)
    set_tests_properties(imageAnalysisTests PROPERTIES LABELS "unit;image")
    ipr_add_test(adjustmentTableTests tests/adjustment_table_test.cpp)
    set_tests_properties(adjustmentTableTests PROPERTIES LABELS "unit")
    ipr_add_test(renderChainTests tests/render_chain_test.cpp)
    set_tests_properties(renderChainTests PROPERTIES LABELS "unit;pipeline")
    ipr_add_test(lineDetectionTests tests/line_detection_test.cpp)
    set_tests_properties(lineDetectionTests PROPERTIES LABELS "unit;detection")
    ipr_add_test(detailProcessingTests tests/detail_processing_test.cpp)
    add_executable(iprGeometryAudit tests/geometry_audit.cpp)
    target_link_libraries(iprGeometryAudit PRIVATE iprEngine)
    add_executable(iprLineAudit tests/line_audit.cpp)
    target_link_libraries(iprLineAudit PRIVATE iprEngine)
    add_executable(iprStackAudit tests/stack_audit.cpp)
    target_link_libraries(iprStackAudit PRIVATE iprEngine)
    if(WIN32)
        target_link_libraries(iprStackAudit PRIVATE psapi)
    endif()
    ipr_add_test(cameraDngTests tests/camera_dng_test.cpp)
    ipr_add_test(noiseLawTests tests/noise_law_test.cpp)
    ipr_add_test(cfaNoiseStageTests tests/cfa_noise_stage_test.cpp)
    set_tests_properties(cfaNoiseStageTests PROPERTIES LABELS "unit;raw;noise" TIMEOUT 30)
    set_tests_properties(noiseLawTests PROPERTIES LABELS "unit;raw;noise" TIMEOUT 30)
    set_tests_properties(cameraDngTests PROPERTIES LABELS "unit;stack;export" TIMEOUT 60)
    add_executable(iprPanoramaAudit tests/panorama_audit.cpp)
    target_link_libraries(iprPanoramaAudit PRIVATE iprEngine)
    if(WIN32)
        target_link_libraries(iprPanoramaAudit PRIVATE psapi)
    endif()
    ipr_add_test(retouchTests tests/retouch_test.cpp)
    set_tests_properties(retouchTests PROPERTIES LABELS "unit;retouch" TIMEOUT 120)
    ipr_add_test(editorRetouchTests tests/editor_retouch_test.cpp)
    set_tests_properties(editorRetouchTests PROPERTIES LABELS "unit;retouch" TIMEOUT 180)
    ipr_add_test(panoramaTests tests/panorama_test.cpp)
    set_tests_properties(panoramaTests PROPERTIES LABELS "unit;panorama" TIMEOUT 180)
    ipr_add_test(stackSessionTests tests/stack_session_test.cpp)
    set_tests_properties(stackSessionTests PROPERTIES LABELS "unit;stack" TIMEOUT 180)
    ipr_add_test(stackTests tests/stack_test.cpp)
    set_tests_properties(stackTests PROPERTIES LABELS "unit;stack" TIMEOUT 180)
    ipr_add_test(geometryTests tests/geometry_test.cpp)
    set_tests_properties(geometryTests PROPERTIES LABELS "unit;geometry" TIMEOUT 60)
    ipr_add_test(localContrastTests tests/local_contrast_test.cpp)
    ipr_add_test(dehazeTests tests/dehaze_test.cpp)
    ipr_add_test(grainTests tests/grain_test.cpp)
    ipr_add_test(colorGradingTests tests/color_grading_test.cpp)
    ipr_add_test(imageExportTests tests/image_export_test.cpp)
    ipr_add_test(editorBatchTests tests/batch_test.cpp)
    set_tests_properties(editorBatchTests PROPERTIES LABELS "unit;export;batch" TIMEOUT 180)
    ipr_add_test(sidecarTests tests/sidecar_test.cpp)
    ipr_add_test(sidecarJsonTests tests/sidecar_json_test.cpp)
    target_sources(sidecarJsonTests PRIVATE src/tools/SidecarJson.cpp)
    target_include_directories(sidecarJsonTests PRIVATE src/tools)
    ipr_add_test(editHistoryTests tests/edit_history_test.cpp)
    ipr_add_test(localAdjustmentsTests tests/local_adjustments_test.cpp)
    add_executable(iprMasksAudit tests/masks_audit.cpp)
    target_link_libraries(iprMasksAudit PRIVATE iprEngine)
    ipr_add_test(localValuesTests tests/local_values_test.cpp)
    ipr_add_test(editorGeometryTests tests/editor_geometry_test.cpp)
    set_tests_properties(editorGeometryTests PROPERTIES LABELS "unit;geometry" TIMEOUT 120)
    ipr_add_test(editorMasksTests tests/editor_masks_test.cpp)
    set_tests_properties(editorMasksTests PROPERTIES LABELS "unit;masks" TIMEOUT 180)
    ipr_add_test(editorHistoryTests tests/editor_history_test.cpp)
    set_tests_properties(editorHistoryTests PROPERTIES LABELS "unit;history" TIMEOUT 120)
    ipr_add_test(folderRefreshTests tests/folder_refresh_test.cpp)
    set_tests_properties(folderRefreshTests PROPERTIES LABELS "unit;batch" TIMEOUT 120)
    ipr_add_test(toneRecoveryTests tests/tone_recovery_test.cpp)
    set_tests_properties(toneRecoveryTests PROPERTIES LABELS "unit;pipeline;lighting")
    add_executable(testImageGeneratorTests tests/test_image_generator_test.cpp)
    target_link_libraries(testImageGeneratorTests PRIVATE iprTestRecipes)
    add_dependencies(testImageGeneratorTests iprGenerateTests)
    add_test(NAME testImageGeneratorTests
        COMMAND testImageGeneratorTests $<TARGET_FILE:iprGenerateTests>)
    set_tests_properties(testImageGeneratorTests PROPERTIES
        LABELS "unit;export;generator" TIMEOUT 120)
    ipr_add_test(colorMixerMathTests tests/color_mixer_math_test.cpp)
    ipr_add_test(pointColorTests tests/point_color_test.cpp)
    ipr_add_test(toneCurveTests tests/tone_curve_test.cpp)
    ipr_add_test(rawInputTests tests/raw_input_test.cpp)
    ipr_add_test(rawDevelopmentTests tests/raw_development_test.cpp)
    ipr_add_test(parallelJobsTests tests/parallel_jobs_test.cpp)
    target_sources(parallelJobsTests PRIVATE src/tools/ParallelJobs.cpp)
    target_include_directories(parallelJobsTests PRIVATE src/tools)
    set_tests_properties(parallelJobsTests PROPERTIES LABELS "unit;generator;thread" TIMEOUT 20)
    ipr_add_test(taskRunnerTests tests/task_runner_test.cpp)
    set_tests_properties(taskRunnerTests PROPERTIES LABELS "unit;thread" TIMEOUT 30)
    set_tests_properties(rawInputTests rawDevelopmentTests PROPERTIES LABELS "unit;raw")
    set_tests_properties(toneCurveTests PROPERTIES LABELS "unit;pipeline;curves")
    set_tests_properties(pointColorTests PROPERTIES LABELS "unit;pipeline;colour")
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
    ipr_add_test(historyQmlTests tests/history_qml_test.cpp)
    target_compile_definitions(historyQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(historyQmlTests PROPERTIES
        LABELS "unit;history;qml" TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(assembleQmlTests tests/assemble_qml_test.cpp)
    target_compile_definitions(assembleQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(assembleQmlTests PROPERTIES
        LABELS "unit;stack;qml" TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(retouchQmlTests tests/retouch_qml_test.cpp)
    target_compile_definitions(retouchQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(retouchQmlTests PROPERTIES
        LABELS "unit;retouch;qml" TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(masksQmlTests tests/masks_qml_test.cpp)
    target_compile_definitions(masksQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(masksQmlTests PROPERTIES
        LABELS "unit;masks;qml" TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(geometryQmlTests tests/geometry_qml_test.cpp)
    target_compile_definitions(geometryQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(geometryQmlTests PROPERTIES
        LABELS "unit;geometry;qml" TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
    ipr_add_test(imageViewportQmlTests tests/image_viewport_qml_test.cpp)
    target_compile_definitions(imageViewportQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(imageViewportQmlTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
        LABELS "unit;viewport;qml")
    ipr_add_test(toneCurveQmlTests tests/tone_curve_qml_test.cpp)
    target_compile_definitions(toneCurveQmlTests PRIVATE
        IPR_QML_DIRECTORY="${IPR_SOURCE_ROOT}/qml")
    set_tests_properties(toneCurveQmlTests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
        LABELS "unit;curves;qml")
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
    if(WIN32)
        add_test(NAME uiPreferencesWindowsStyleTests COMMAND uiPreferencesQmlTests)
        set_tests_properties(uiPreferencesWindowsStyleTests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Windows"
            LABELS "unit;preferences;qml")
    endif()
    ipr_add_test(rawDecoderTests tests/raw_decoder_test.cpp)
    ipr_add_test(demosaicTests tests/demosaic_test.cpp)
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
    set_tests_properties(rawDecoderTests demosaicTests rawBlackLevelEstimatorTests rawNormalizationTests
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
