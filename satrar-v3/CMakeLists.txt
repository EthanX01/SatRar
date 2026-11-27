cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()
project(satrar-v3 LANGUAGES CXX)

if(MSVC AND NOT POLICY CMP0141)
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SATRAR_V1 "${CMAKE_CURRENT_SOURCE_DIR}/../satrar-cpp")
set(SATRAR_MINISAT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../minisat-solver" CACHE PATH "MiniSat source tree")
set(SATRAR_ABC_EXECUTABLE "${SATRAR_V1}/tools/abc/bin/yosys-abc.exe"
    CACHE FILEPATH "ABC executable used for every equivalence check")

add_library(satrar_v3 STATIC
    "${SATRAR_V1}/src/aig/Literal.cpp"
    "${SATRAR_V1}/src/aig/Circuit.cpp"
    "${SATRAR_V1}/src/aig/AagParser.cpp"
    "${SATRAR_V1}/src/aig/AagWriter.cpp"
    "${SATRAR_V1}/src/sat/CnfEncoder.cpp"
    "${SATRAR_V1}/src/sat/MiterBuilder.cpp"
    "${SATRAR_V1}/src/sat/SatBackend.cpp"
    "${SATRAR_V1}/src/analysis/GraphAnalysis.cpp"
    "${SATRAR_V1}/src/analysis/CircuitPropagator.cpp"
    "${SATRAR_V1}/src/analysis/MandatoryAssignments.cpp"
    "${SATRAR_V1}/src/analysis/FaultContext.cpp"
    "${SATRAR_V1}/src/rar/SatRarEngine.cpp"
    "${SATRAR_V1}/src/rar/CircuitRewriter.cpp"
    "${SATRAR_V1}/src/rar/Rewriter.cpp"
    src/aig/AigWriter.cpp
    src/aig/DenseAig.cpp
    src/rar/GoldenContext.cpp
    src/rar/CandidateBuilder.cpp
    src/sat/ProcessRunner.cpp
    src/cec/AbcLog.cpp
    src/cec/AbcCec.cpp
    src/rar/EquivalenceChecker.cpp
    src/rar/RarVerifier.cpp
    src/rar/ExportAag.cpp
    src/rar/BatchRarVerifier.cpp
    src/app/CommandShell.cpp
)
target_include_directories(satrar_v3 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${SATRAR_V1}/include"
)
target_compile_definitions(satrar_v3 PUBLIC SATRAR_CONFIGURED_ABC="${SATRAR_ABC_EXECUTABLE}")

if(MSVC)
    target_compile_options(satrar_v3 PUBLIC /utf-8)
    target_compile_options(satrar_v3 PRIVATE /W4)
else()
    target_compile_options(satrar_v3 PRIVATE -Wall -Wextra -pedantic)
endif()

option(SATRAR_BUILD_MINISAT "Build MiniSat propagation backend" ON)
if(SATRAR_BUILD_MINISAT)
    add_library(satrar_v3_sat STATIC
        "${SATRAR_V1}/third_party/vendored_minisat/Solver.cc"
        "${SATRAR_MINISAT_ROOT}/minisat/utils/Options.cc"
        "${SATRAR_MINISAT_ROOT}/minisat/utils/System.cc"
        "${SATRAR_V1}/src/sat/MinisatPropagationEngine.cpp"
        "${SATRAR_V1}/src/sat/MinisatVerificationSolver.cpp"
    )
    target_include_directories(satrar_v3_sat PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${SATRAR_V1}/include"
        "${SATRAR_MINISAT_ROOT}"
        "${SATRAR_V1}/third_party/compat"
    )
    if(MSVC)
        target_compile_options(satrar_v3_sat PRIVATE /W3 /wd4244 /wd4267 /utf-8)
    endif()
    target_link_libraries(satrar_v3 PUBLIC satrar_v3_sat)
    target_compile_definitions(satrar_v3 PUBLIC SATRAR_HAS_MINISAT=1)
endif()

add_executable(satrar app/main.cpp)
target_link_libraries(satrar PRIVATE satrar_v3)
if(MSVC)
    target_compile_options(satrar PRIVATE /utf-8 /W4)
else()
    target_compile_options(satrar PRIVATE -Wall -Wextra -pedantic)
endif()

enable_testing()

function(satrar_v3_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE satrar_v3)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /W4)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

satrar_v3_test(test_aig_writer tests/test_aig_writer.cpp)
satrar_v3_test(test_abc_log tests/test_abc_log.cpp)
satrar_v3_test(test_process_runner tests/test_process_runner.cpp)
satrar_v3_test(test_abc_cec tests/test_abc_cec.cpp)
satrar_v3_test(test_verification_stages tests/test_verification_stages.cpp)
satrar_v3_test(test_batch_verifier tests/test_batch_verifier.cpp)
satrar_v3_test(test_candidate_builder tests/test_candidate_builder.cpp)
set_tests_properties(test_process_runner test_abc_cec test_verification_stages test_batch_verifier
                     PROPERTIES SKIP_RETURN_CODE 77)
