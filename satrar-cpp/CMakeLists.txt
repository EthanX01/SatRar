cmake_minimum_required(VERSION 3.14)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()
project(satrar-cpp LANGUAGES CXX)

if(MSVC AND NOT POLICY CMP0141)
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SATRAR_MINISAT_ROOT "${CMAKE_SOURCE_DIR}/../minisat-solver" CACHE PATH "MiniSat source tree (read-only)")

add_library(satrar_core STATIC
    src/aig/Literal.cpp
    src/aig/Circuit.cpp
    src/aig/AagParser.cpp
    src/aig/AagWriter.cpp
    src/sat/CnfEncoder.cpp
    src/sat/MiterBuilder.cpp
    src/app/CommandShell.cpp
    src/analysis/GraphAnalysis.cpp
    src/analysis/CircuitPropagator.cpp
    src/analysis/MandatoryAssignments.cpp
    src/analysis/FaultContext.cpp
    src/rar/SatRarEngine.cpp
    src/rar/CircuitRewriter.cpp
    src/rar/EquivalenceChecker.cpp
    src/rar/Rewriter.cpp
    src/rar/RarVerifier.cpp
    src/rar/ExportAag.cpp
    src/sat/SatBackend.cpp
    src/sat/CadicalBackend.cpp
)
target_include_directories(satrar_core PUBLIC "${CMAKE_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(satrar_core PUBLIC /utf-8)
    target_compile_options(satrar_core PRIVATE /W4)
else()
    target_compile_options(satrar_core PRIVATE -Wall -Wextra -pedantic)
endif()

set(SATRAR_CADICAL_EXECUTABLE "" CACHE FILEPATH "Override the CaDiCaL executable")
option(SATRAR_BUILD_CADICAL "Build the vendored CaDiCaL CLI" ON)
if(SATRAR_BUILD_CADICAL)
    set(cadical_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cadical/src")
    file(GLOB cadical_sources CONFIGURE_DEPENDS "${cadical_dir}/*.cpp")
    list(FILTER cadical_sources EXCLUDE REGEX "/mobical\\.cpp$")
    add_executable(satrar_cadical ${cadical_sources})
    target_compile_definitions(satrar_cadical PRIVATE NBUILD)
    if(WIN32)
        target_compile_definitions(satrar_cadical PRIVATE __WIN32 NOMINMAX)
        target_link_libraries(satrar_cadical PRIVATE psapi)
    endif()
    if(MSVC)
        target_include_directories(satrar_cadical PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/compat")
        target_compile_definitions(satrar_cadical PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
        target_compile_options(satrar_cadical PRIVATE /MP /utf-8 /FIunistd.h)
    endif()
    add_dependencies(satrar_core satrar_cadical)
    target_compile_definitions(satrar_core PRIVATE SATRAR_BUNDLED_CADICAL="$<TARGET_FILE:satrar_cadical>")
    install(TARGETS satrar_cadical DESTINATION bin)
endif()
if(SATRAR_CADICAL_EXECUTABLE)
    target_compile_definitions(satrar_core PRIVATE SATRAR_CONFIGURED_CADICAL="${SATRAR_CADICAL_EXECUTABLE}")
endif()
option(SATRAR_BUILD_MINISAT "Build MiniSat propagation backend" ON)
if(SATRAR_BUILD_MINISAT)
    add_library(satrar_sat STATIC
        "${CMAKE_SOURCE_DIR}/third_party/vendored_minisat/Solver.cc"
        "${SATRAR_MINISAT_ROOT}/minisat/utils/Options.cc"
        "${SATRAR_MINISAT_ROOT}/minisat/utils/System.cc"
        src/sat/MinisatPropagationEngine.cpp
        src/sat/MinisatVerificationSolver.cpp
    )
    target_include_directories(satrar_sat PUBLIC
        "${CMAKE_SOURCE_DIR}/include"
        "${SATRAR_MINISAT_ROOT}"
        "${CMAKE_SOURCE_DIR}/third_party/compat"
    )
    if(MSVC)
        target_compile_options(satrar_sat PRIVATE /W3 /wd4244 /wd4267 /utf-8)
    endif()

    target_link_libraries(satrar_core PUBLIC satrar_sat)
    target_compile_definitions(satrar_core PUBLIC SATRAR_HAS_MINISAT=1)
endif()

add_executable(satrar app/main.cpp)
if(SATRAR_BUILD_MINISAT)
    target_link_libraries(satrar PRIVATE satrar_core satrar_sat)
else()
    target_link_libraries(satrar PRIVATE satrar_core)
endif()

if(MSVC)
    target_compile_options(satrar PRIVATE /utf-8 /W4)
else()
    target_compile_options(satrar PRIVATE -Wall -Wextra -pedantic)
endif()

enable_testing()

function(satrar_add_test name source)
    add_executable(${name} ${source})
    if(SATRAR_BUILD_MINISAT)
        target_link_libraries(${name} PRIVATE satrar_core satrar_sat)
    else()
        target_link_libraries(${name} PRIVATE satrar_core)
    endif()
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

satrar_add_test(test_literal tests/test_literal.cpp)
satrar_add_test(test_wire tests/test_wire.cpp)
satrar_add_test(test_aag tests/test_aag.cpp)
satrar_add_test(test_graph tests/test_graph.cpp)
satrar_add_test(test_aag_invariants tests/test_aag_invariants.cpp)
satrar_add_test(test_benchmark_smoke tests/test_benchmark_smoke.cpp)

if(SATRAR_BUILD_MINISAT)
    add_executable(test_minisat_link tests/test_minisat_link.cpp)
    target_link_libraries(test_minisat_link PRIVATE satrar_sat)
    if(MSVC)
        target_compile_options(test_minisat_link PRIVATE /utf-8)
    endif()
    add_test(NAME test_minisat_link COMMAND test_minisat_link)

    satrar_add_test(test_minisat_propagation tests/test_minisat_propagation.cpp)
    satrar_add_test(test_fault tests/test_fault.cpp)
    satrar_add_test(test_rar_l0 tests/test_rar_l0.cpp)
    satrar_add_test(test_srar_wire tests/test_srar_wire.cpp)
    satrar_add_test(test_rar_discover tests/test_rar_discover.cpp)
    satrar_add_test(test_rewriter tests/test_rewriter.cpp)
    satrar_add_test(test_circuit_propagator tests/test_circuit_propagator.cpp)
    satrar_add_test(test_miter_cnf_regression tests/test_miter_cnf_regression.cpp)
    satrar_add_test(test_oracle_differential tests/test_oracle_differential.cpp)
    target_include_directories(test_oracle_differential PRIVATE tests)
    satrar_add_test(test_paper_anchors tests/test_paper_anchors.cpp)
    satrar_add_test(test_propagator_differential tests/test_propagator_differential.cpp)
    target_include_directories(test_propagator_differential PRIVATE tests)
    satrar_add_test(test_provenance_theorems tests/test_provenance_theorems.cpp)
    target_include_directories(test_provenance_theorems PRIVATE tests)
endif()

satrar_add_test(test_cadical_backend tests/test_cadical_backend.cpp)
satrar_add_test(test_equivalence tests/test_equivalence.cpp)
satrar_add_test(test_verification_stages tests/test_verification_stages.cpp)
set_tests_properties(test_cadical_backend test_equivalence test_verification_stages PROPERTIES SKIP_RETURN_CODE 77)

install(TARGETS satrar DESTINATION bin)
