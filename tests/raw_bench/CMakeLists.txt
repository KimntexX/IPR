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

# C2 module bench: compile the committed development sources in this worktree,
# never link an engine or CMake cache from another agent's build.
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
get_filename_component(IPR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(IPR_RAW_SOURCE_ROOT "${IPR_ROOT}" CACHE PATH "Frozen engine source for module capture")
set(IPR_RAW_ENGINE_REVISION "${IPR_BENCH_REVISION}" CACHE STRING "Revision of the captured engine")
add_executable(iprRawDevelopmentCapture development_capture.cpp
    "${IPR_RAW_SOURCE_ROOT}/src/raw/RawInput.cpp" "${IPR_RAW_SOURCE_ROOT}/src/raw/LibRawInput.cpp"
    "${IPR_RAW_SOURCE_ROOT}/src/raw/RawDevelopment.cpp" "${IPR_RAW_SOURCE_ROOT}/src/raw/Demosaic.cpp")
target_include_directories(iprRawDevelopmentCapture PRIVATE "${IPR_RAW_SOURCE_ROOT}/src/raw" "${LIBRAW_INCLUDE_DIR}" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(iprRawDevelopmentCapture PRIVATE Qt6::Core "${LIBRAW_LIBRARY}" ${OpenCV_LIBS})
target_compile_definitions(iprRawDevelopmentCapture PRIVATE IPR_WITH_LIBRAW=1
    IPR_BENCH_REVISION="${IPR_BENCH_REVISION}" IPR_RAW_ENGINE_REVISION="${IPR_RAW_ENGINE_REVISION}")
