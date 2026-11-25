cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()
project(satrar-circuit-v2 LANGUAGES CXX)

if(MSVC AND NOT POLICY CMP0141)
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(satrar_v2_aig STATIC
    src/aig/Literal.cpp
    src/aig/Circuit.cpp
    src/aig/AagParser.cpp
    src/aig/AagWriter.cpp
)
target_include_directories(satrar_v2_aig PUBLIC "${CMAKE_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(satrar_v2_aig PUBLIC /utf-8)
    target_compile_options(satrar_v2_aig PRIVATE /W4)
else()
    target_compile_options(satrar_v2_aig PRIVATE -Wall -Wextra -pedantic)
endif()

add_library(satrar_v2_solver STATIC
    src/solver/GateNet.cpp
    src/solver/SolverSession.cpp
)
target_link_libraries(satrar_v2_solver PUBLIC satrar_v2_aig)
if(MSVC)
    target_compile_options(satrar_v2_solver PRIVATE /W4)
else()
    target_compile_options(satrar_v2_solver PRIVATE -Wall -Wextra -pedantic)
endif()

enable_testing()

function(satrar_v2_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${ARGN})
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /W4)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_library(satrar_v2_rar STATIC
    src/analysis/GraphAnalysis.cpp
    src/analysis/MandatoryAssignments.cpp
    src/analysis/FaultContext.cpp
    src/rar/SatRarEngine.cpp
    src/rar/Rewriter.cpp
)
target_link_libraries(satrar_v2_rar PUBLIC satrar_v2_solver)
if(MSVC)
    target_compile_options(satrar_v2_rar PRIVATE /W4)
else()
    target_compile_options(satrar_v2_rar PRIVATE -Wall -Wextra -pedantic)
endif()

set(SATRAR_V1_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../satrar-cpp/third_party" CACHE PATH
    "V1 third_party tree used for the verification CaDiCaL build")
option(SATRAR_BUILD_CADICAL "Build the V1-vendored CaDiCaL CLI into this project" ON)
if(SATRAR_BUILD_CADICAL)
    set(cadical_dir "${SATRAR_V1_THIRD_PARTY}/cadical/src")
    file(GLOB cadical_sources CONFIGURE_DEPENDS "${cadical_dir}/*.cpp")
    list(FILTER cadical_sources EXCLUDE REGEX "/mobical\\.cpp$")
    add_executable(satrar_cadical ${cadical_sources})
    target_compile_definitions(satrar_cadical PRIVATE NBUILD)
    if(WIN32)
        target_compile_definitions(satrar_cadical PRIVATE __WIN32 NOMINMAX)
        target_link_libraries(satrar_cadical PRIVATE psapi)
    endif()
    if(MSVC)
        target_include_directories(satrar_cadical PRIVATE "${SATRAR_V1_THIRD_PARTY}/compat")
        target_compile_definitions(satrar_cadical PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
        target_compile_options(satrar_cadical PRIVATE /MP /utf-8 /FIunistd.h)
    endif()
endif()

add_library(satrar_v2_verify STATIC
    src/sat/CnfEncoder.cpp
    src/sat/MiterBuilder.cpp
    src/sat/SatBackend.cpp
    src/sat/CadicalBackend.cpp
    src/rar/EquivalenceChecker.cpp
    src/rar/RarVerifier.cpp
    src/rar/ExportAag.cpp
)
target_link_libraries(satrar_v2_verify PUBLIC satrar_v2_aig satrar_v2_rar)
if(SATRAR_BUILD_CADICAL)
    add_dependencies(satrar_v2_verify satrar_cadical)
    target_compile_definitions(satrar_v2_verify PRIVATE SATRAR_BUNDLED_CADICAL="$<TARGET_FILE:satrar_cadical>")
endif()
if(MSVC)
    target_compile_options(satrar_v2_verify PRIVATE /W4)
else()
    target_compile_options(satrar_v2_verify PRIVATE -Wall -Wextra -pedantic)
endif()

add_executable(satrar_v2 app/main.cpp src/app/CommandShell.cpp)
target_link_libraries(satrar_v2 PRIVATE satrar_v2_rar satrar_v2_verify)
if(MSVC)
    target_compile_options(satrar_v2 PRIVATE /utf-8 /W4)
endif()

satrar_v2_test(test_gate_bcp tests/test_gate_bcp.cpp satrar_v2_solver)
satrar_v2_test(test_fault_ma tests/test_fault_ma.cpp satrar_v2_rar)
satrar_v2_test(test_rar_anchors tests/test_rar_anchors.cpp satrar_v2_rar)
satrar_v2_test(test_exact tests/test_exact.cpp satrar_v2_rar)
satrar_v2_test(test_aag_roundtrip tests/test_aag_roundtrip.cpp satrar_v2_aig)
satrar_v2_test(test_cec tests/test_cec.cpp satrar_v2_verify)
set_tests_properties(test_cec PROPERTIES SKIP_RETURN_CODE 77)
