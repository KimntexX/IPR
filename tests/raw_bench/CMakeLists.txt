cmake_minimum_required(VERSION 3.21)
project(IPRRawBench LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
find_path(LIBRAW_INCLUDE_DIR libraw/libraw.h REQUIRED)
find_library(LIBRAW_LIBRARY NAMES raw libraw PATH_SUFFIXES manual-link REQUIRED)
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE IPR_BENCH_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
add_executable(iprRawUnpackCapture unpack_capture.cpp)
target_include_directories(iprRawUnpackCapture PRIVATE "${LIBRAW_INCLUDE_DIR}")
target_link_libraries(iprRawUnpackCapture PRIVATE Qt6::Core "${LIBRAW_LIBRARY}")
target_compile_definitions(iprRawUnpackCapture PRIVATE IPR_BENCH_REVISION="${IPR_BENCH_REVISION}")
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME rawBenchCaptureContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_capture.py)
add_test(NAME rawBenchProductionContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_production_campaign.py)
add_test(NAME rawBenchSignedMeasurement COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_signed*.py")
add_test(NAME rawBenchFloatInlet COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_float_qualification.py)
add_test(NAME rawBenchSignalSurvey COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_noise_survey.py)
add_test(NAME rawBenchSignalDenoiseReference COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_denoise.py)

# C2 module bench: compile the committed development sources in this worktree,
# never link an engine or CMake cache from another agent's build.
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(Threads REQUIRED)
get_filename_component(IPR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(cameraDenoiseTests test_camera_denoise.cpp "${IPR_ROOT}/src/raw/CameraDenoise.cpp")
target_include_directories(cameraDenoiseTests PRIVATE "${IPR_ROOT}/src/raw" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(cameraDenoiseTests PRIVATE ${OpenCV_LIBS} Threads::Threads)
if(WIN32)
    target_link_libraries(cameraDenoiseTests PRIVATE Psapi)
endif()
add_test(NAME cameraDenoiseContract COMMAND cameraDenoiseTests)
set_tests_properties(cameraDenoiseContract PROPERTIES TIMEOUT 120)
add_executable(cfaDenoisePrototype cfa_denoise_prototype.cpp
    "${IPR_ROOT}/src/raw/Demosaic.cpp" "${IPR_ROOT}/src/raw/CameraDenoise.cpp")
target_include_directories(cfaDenoisePrototype PRIVATE "${IPR_ROOT}/src/raw" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(cfaDenoisePrototype PRIVATE Qt6::Core ${OpenCV_LIBS} Threads::Threads)
add_test(NAME cfaDenoisePrototypeContract COMMAND cfaDenoisePrototype --self-test)
set_tests_properties(cfaDenoisePrototypeContract PROPERTIES TIMEOUT 30)
add_executable(denoiseTonalGainProbe tonal_gain_probe.cpp "${IPR_ROOT}/src/raw/CameraDenoise.cpp"
    "${IPR_ROOT}/src/core/Contrast.cpp" "${IPR_ROOT}/src/core/ToneRecovery.cpp" "${IPR_ROOT}/src/core/ToneStatistics.cpp")
target_include_directories(denoiseTonalGainProbe PRIVATE "${IPR_ROOT}/src/raw" "${IPR_ROOT}/src/core" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(denoiseTonalGainProbe PRIVATE Qt6::Core ${OpenCV_LIBS} Threads::Threads)
add_test(NAME denoiseTonalGainContract COMMAND denoiseTonalGainProbe)
set_tests_properties(denoiseTonalGainContract PROPERTIES TIMEOUT 30)
add_executable(weakStructureProbe weak_structure_probe.cpp "${IPR_ROOT}/src/raw/CameraDenoise.cpp")
target_include_directories(weakStructureProbe PRIVATE "${IPR_ROOT}/src/raw" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(weakStructureProbe PRIVATE Qt6::Core ${OpenCV_LIBS} Threads::Threads)
add_test(NAME weakStructureSummaryContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_weak_structure.py)
set_tests_properties(weakStructureSummaryContract PROPERTIES TIMEOUT 30)
add_test(NAME structurePredictionContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_structure_prediction.py)
set_tests_properties(structurePredictionContract PROPERTIES TIMEOUT 30)
add_executable(structureBaselineProbe structure_baseline_probe.cpp "${IPR_ROOT}/src/raw/CameraDenoise.cpp")
target_include_directories(structureBaselineProbe PRIVATE "${IPR_ROOT}/src/raw" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(structureBaselineProbe PRIVATE Qt6::Core ${OpenCV_LIBS} Threads::Threads)
add_test(NAME structureAmplitudeFreeContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_structure_amplitude_free.py)
set_tests_properties(structureAmplitudeFreeContract PROPERTIES TIMEOUT 30)
set(IPR_ORACLE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/oracle-generated/CameraDenoiseOracle.cpp")
add_custom_command(OUTPUT "${IPR_ORACLE_SOURCE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_noise_oracle.py"
        --source "${IPR_ROOT}/src/raw/CameraDenoise.cpp" --output "${IPR_ORACLE_SOURCE}"
    DEPENDS "${IPR_ROOT}/src/raw/CameraDenoise.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/generate_noise_oracle.py"
    VERBATIM)
add_executable(structureOracleProbe structure_baseline_probe.cpp noise_bench_control.cpp "${IPR_ORACLE_SOURCE}")
target_compile_definitions(structureOracleProbe PRIVATE IPR_BENCH_ORACLE=1)
target_include_directories(structureOracleProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${IPR_ROOT}/src/raw" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(structureOracleProbe PRIVATE Qt6::Core ${OpenCV_LIBS} Threads::Threads)
add_test(NAME noiseModelAblationContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_noise_model_ablation.py)
set_tests_properties(noiseModelAblationContract PROPERTIES TIMEOUT 30)
add_test(NAME signalNoiseBridgeContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_noise_bridge.py)
set_tests_properties(signalNoiseBridgeContract PROPERTIES TIMEOUT 30)
add_executable(cfaTransportProbe cfa_transport_probe.cpp "${IPR_ROOT}/src/raw/Demosaic.cpp")
target_include_directories(cfaTransportProbe PRIVATE "${IPR_ROOT}/src/raw" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(cfaTransportProbe PRIVATE Qt6::Core ${OpenCV_LIBS} Threads::Threads)
add_test(NAME cfaTransportSignedContract COMMAND cfaTransportProbe --self-test)
set_tests_properties(cfaTransportSignedContract PROPERTIES TIMEOUT 30)
add_test(NAME cfaSignalTransportContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_cfa_signal_transport.py)
set_tests_properties(cfaSignalTransportContract PROPERTIES TIMEOUT 30)
add_test(NAME signalFilterContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_filter.py)
set_tests_properties(signalFilterContract PROPERTIES TIMEOUT 30)
add_test(NAME signalDistributionContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_noise_draws.py)
set_tests_properties(signalDistributionContract PROPERTIES TIMEOUT 30)
add_test(NAME signalFilterAuditContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_filter_audit.py)
set_tests_properties(signalFilterAuditContract PROPERTIES TIMEOUT 30)
add_test(NAME signalCovarianceContract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_signal_noise_covariance.py)
set_tests_properties(signalCovarianceContract PROPERTIES TIMEOUT 30)
set(IPR_RAW_SOURCE_ROOT "${IPR_ROOT}" CACHE PATH "Frozen engine source for module capture")
set(IPR_RAW_ENGINE_REVISION "" CACHE STRING "Explicit revision of an archived engine; empty uses worktree HEAD")
if(NOT IPR_RAW_ENGINE_REVISION)
    if(NOT IPR_RAW_SOURCE_ROOT STREQUAL IPR_ROOT)
        message(FATAL_ERROR "An archived engine needs an explicit IPR_RAW_ENGINE_REVISION")
    endif()
    set(IPR_RAW_ENGINE_REVISION "${IPR_BENCH_REVISION}")
endif()
add_executable(iprRawDevelopmentCapture development_capture.cpp
    "${IPR_RAW_SOURCE_ROOT}/src/raw/RawInput.cpp" "${IPR_RAW_SOURCE_ROOT}/src/raw/LibRawInput.cpp"
    "${IPR_RAW_SOURCE_ROOT}/src/raw/RawDevelopment.cpp" "${IPR_RAW_SOURCE_ROOT}/src/raw/Demosaic.cpp")
target_include_directories(iprRawDevelopmentCapture PRIVATE "${IPR_RAW_SOURCE_ROOT}/src/raw" "${LIBRAW_INCLUDE_DIR}" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(iprRawDevelopmentCapture PRIVATE Qt6::Core "${LIBRAW_LIBRARY}" ${OpenCV_LIBS})
target_compile_definitions(iprRawDevelopmentCapture PRIVATE IPR_WITH_LIBRAW=1
    IPR_BENCH_REVISION="${IPR_BENCH_REVISION}" IPR_RAW_ENGINE_REVISION="${IPR_RAW_ENGINE_REVISION}")
