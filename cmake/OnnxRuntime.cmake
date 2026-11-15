# Official binary packages are fetched separately by scripts/neural_assets.py.
# No network access or implicit dependency installation during configuration.
set(IPR_ONNXRUNTIME_ROOT "" CACHE PATH "Extracted ONNX Runtime SDK root")
set(IPR_DIRECTML_ROOT "" CACHE PATH "Extracted Microsoft.AI.DirectML SDK root")
option(IPR_NEURAL_DIRECTML "Use DirectML on Windows, with CPU fallback" ON)

find_path(IPR_ORT_INCLUDE onnxruntime_cxx_api.h
    HINTS "${IPR_ONNXRUNTIME_ROOT}/build/native/include" "${IPR_ONNXRUNTIME_ROOT}/include" REQUIRED)
find_library(IPR_ORT_LIBRARY NAMES onnxruntime
    HINTS "${IPR_ONNXRUNTIME_ROOT}/runtimes/win-x64/native" "${IPR_ONNXRUNTIME_ROOT}/lib" REQUIRED)
add_library(iprOnnxRuntime UNKNOWN IMPORTED)
set_target_properties(iprOnnxRuntime PROPERTIES IMPORTED_LOCATION "${IPR_ORT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${IPR_ORT_INCLUDE}")
target_link_libraries(iprNeural PRIVATE iprOnnxRuntime)
target_compile_definitions(iprNeural PRIVATE IPR_HAS_ONNX_RUNTIME)

if(WIN32)
    find_file(IPR_ORT_DLL onnxruntime.dll HINTS "${IPR_ONNXRUNTIME_ROOT}/runtimes/win-x64/native" REQUIRED)
    set(IPR_NEURAL_RUNTIME_FILES "${IPR_ORT_DLL}")
    find_file(IPR_ORT_SHARED_DLL onnxruntime_providers_shared.dll
        HINTS "${IPR_ONNXRUNTIME_ROOT}/runtimes/win-x64/native")
    if(IPR_ORT_SHARED_DLL)
        list(APPEND IPR_NEURAL_RUNTIME_FILES "${IPR_ORT_SHARED_DLL}")
    endif()
    if(IPR_NEURAL_DIRECTML)
        find_path(IPR_DML_INCLUDE DirectML.h HINTS "${IPR_DIRECTML_ROOT}/include" REQUIRED)
        find_file(IPR_DML_DLL DirectML.dll HINTS "${IPR_DIRECTML_ROOT}/bin/x64-win" REQUIRED)
        target_include_directories(iprNeural PRIVATE "${IPR_DML_INCLUDE}")
        target_compile_definitions(iprNeural PRIVATE IPR_HAS_DIRECTML NOMINMAX WIN32_LEAN_AND_MEAN)
        list(APPEND IPR_NEURAL_RUNTIME_FILES "${IPR_DML_DLL}")
    endif()
    install(FILES ${IPR_NEURAL_RUNTIME_FILES} DESTINATION bin)
endif()
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../licenses/onnxruntime/LICENSE"
    "${CMAKE_CURRENT_LIST_DIR}/../licenses/onnxruntime/ThirdPartyNotices.txt"
    DESTINATION licenses/onnxruntime)
if(WIN32 AND IPR_NEURAL_DIRECTML)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/../licenses/directml/LICENSE.txt"
        "${CMAKE_CURRENT_LIST_DIR}/../licenses/directml/LICENSE-CODE.txt"
        DESTINATION licenses/directml)
endif()

