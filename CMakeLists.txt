# CSTPSI CMake Configuration
# Requires: SEAL 4.x, FLINT, GMP, MPFR, ZeroMQ, cppzmq, nlohmann_json, OpenMP

cmake_minimum_required(VERSION 3.15)

project(CSTPSI VERSION 1.0.0 LANGUAGES C CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ──────────────────────────────────────────────────────────────
# Dependency: Microsoft SEAL
# ──────────────────────────────────────────────────────────────
find_package(SEAL 3.5 QUIET)
if(NOT SEAL_FOUND)
    find_package(SEAL QUIET)
    if(NOT SEAL_FOUND)
        message(FATAL_ERROR "SEAL library not found. Install with:\n"
                            "  macOS: brew install seal\n"
                            "  Ubuntu: apt-get install libseal-dev")
    endif()
endif()

set(SEAL_TARGET "SEAL::seal")
if(TARGET SEAL::seal_shared)
    set(SEAL_TARGET "SEAL::seal_shared")
endif()
if(NOT TARGET ${SEAL_TARGET})
    message(FATAL_ERROR "Could not find SEAL target")
endif()

# ──────────────────────────────────────────────────────────────
# Dependency: nlohmann_json
# ──────────────────────────────────────────────────────────────
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(nlohmann_json QUIET nlohmann_json)
    endif()

    if(NOT nlohmann_json_FOUND)
        find_path(NLOHMANN_JSON_INCLUDE_DIR
            NAMES nlohmann/json.hpp
            PATHS ${CMAKE_PREFIX_PATH}/include
                  /usr/local/include
                  /usr/include
                  /opt/homebrew/opt/nlohmann-json/include
            NO_DEFAULT_PATH
        )
        find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp)

        if(NLOHMANN_JSON_INCLUDE_DIR)
            message(STATUS "Found nlohmann_json headers at: ${NLOHMANN_JSON_INCLUDE_DIR}")
            set(nlohmann_json_INCLUDE_DIRS ${NLOHMANN_JSON_INCLUDE_DIR})
        else()
            message(WARNING "nlohmann_json not found, assuming header-only in system paths")
        endif()
    endif()
endif()

# ──────────────────────────────────────────────────────────────
# Dependency: FLINT, GMP, MPFR, MPIR
# ──────────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(FLINT flint)
    pkg_check_modules(GMP gmp)
    pkg_check_modules(MPFR mpfr)
endif()

if(NOT FLINT_FOUND)
    find_library(FLINT_LIBRARY NAMES flint
        PATHS ${CMAKE_PREFIX_PATH}/lib /usr/local/lib /usr/lib /opt/homebrew/lib
        NO_DEFAULT_PATH)
    find_library(FLINT_LIBRARY NAMES flint)

    find_path(FLINT_INCLUDE_DIR flint/flint.h
        PATHS ${CMAKE_PREFIX_PATH}/include /usr/local/include /usr/include /opt/homebrew/include
        NO_DEFAULT_PATH)
    find_path(FLINT_INCLUDE_DIR flint/flint.h)

    if(FLINT_LIBRARY AND FLINT_INCLUDE_DIR)
        set(FLINT_LIBRARIES ${FLINT_LIBRARY})
        set(FLINT_INCLUDE_DIRS ${FLINT_INCLUDE_DIR})
    endif()
endif()

if(NOT GMP_FOUND)
    find_library(GMP_LIBRARY NAMES gmp
        PATHS ${CMAKE_PREFIX_PATH}/lib /usr/local/lib /usr/lib /opt/homebrew/lib)
    find_library(MPFR_LIBRARY NAMES mpfr
        PATHS ${CMAKE_PREFIX_PATH}/lib /usr/local/lib /usr/lib /opt/homebrew/lib)
    find_library(MPIR_LIBRARY NAMES mpir
        PATHS ${CMAKE_PREFIX_PATH}/lib /usr/local/lib /usr/lib /opt/homebrew/lib)

    if(GMP_LIBRARY)
        set(GMP_LIBRARIES ${GMP_LIBRARY})
    endif()
    if(MPFR_LIBRARY)
        set(MPFR_LIBRARIES ${MPFR_LIBRARY})
    endif()
    if(MPIR_LIBRARY)
        set(MPIR_LIBRARIES ${MPIR_LIBRARY})
    endif()
endif()

set(MATH_LIBRARIES "")
if(FLINT_LIBRARIES)
    list(APPEND MATH_LIBRARIES ${FLINT_LIBRARIES})
endif()
if(GMP_LIBRARIES)
    list(APPEND MATH_LIBRARIES ${GMP_LIBRARIES})
endif()
if(MPFR_LIBRARIES)
    list(APPEND MATH_LIBRARIES ${MPFR_LIBRARIES})
endif()
if(MPIR_LIBRARIES)
    list(APPEND MATH_LIBRARIES ${MPIR_LIBRARIES})
endif()

if(NOT MATH_LIBRARIES)
    message(STATUS "Using static library fallback for FLINT/GMP/MPFR")
    set(MATH_LIBRARIES
        /usr/local/lib/libflint.a
        /usr/local/lib/libgmp.a
        /usr/local/lib/libmpfr.a
        /usr/local/lib/libmpir.a
    )
endif()

# ──────────────────────────────────────────────────────────────
# Dependency: ZeroMQ + cppzmq
# ──────────────────────────────────────────────────────────────
if(PKG_CONFIG_FOUND)
    pkg_check_modules(ZMQ QUIET libzmq)
    pkg_check_modules(CPPZMQ QUIET cppzmq)
endif()

if(NOT ZMQ_FOUND)
    find_library(ZMQ_LIBRARY NAMES zmq
        PATHS ${CMAKE_PREFIX_PATH}/lib /usr/local/lib /usr/lib /opt/homebrew/lib
    )
    find_path(ZMQ_INCLUDE_DIR zmq.h
        PATHS ${CMAKE_PREFIX_PATH}/include /usr/local/include /usr/include /opt/homebrew/include
    )
    if(ZMQ_LIBRARY AND ZMQ_INCLUDE_DIR)
        set(ZMQ_LIBRARIES ${ZMQ_LIBRARY})
        set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
        set(ZMQ_FOUND TRUE)
    endif()
endif()

if(NOT CPPZMQ_FOUND)
    find_path(CPPZMQ_INCLUDE_DIR zmq.hpp
        PATHS ${CMAKE_PREFIX_PATH}/include /usr/local/include /usr/include /opt/homebrew/include
    )
    if(CPPZMQ_INCLUDE_DIR)
        set(CPPZMQ_INCLUDE_DIRS ${CPPZMQ_INCLUDE_DIR})
    endif()
endif()

if(NOT ZMQ_FOUND)
    message(FATAL_ERROR "ZeroMQ not found. Install with:\n"
                        "  Ubuntu: apt-get install libzmq3-dev cppzmq-dev\n"
                        "  macOS:  brew install zeromq cppzmq")
endif()

message(STATUS "Found ZeroMQ: ${ZMQ_LIBRARIES}")
if(CPPZMQ_INCLUDE_DIRS)
    message(STATUS "Found cppzmq headers: ${CPPZMQ_INCLUDE_DIRS}")
endif()

# ──────────────────────────────────────────────────────────────
# Dependency: OpenMP
# ──────────────────────────────────────────────────────────────
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(OPENMP_FOUND_VIA "find_package")
elseif(EXISTS /opt/homebrew/opt/libomp/include)
    # Apple Clang does not expose OpenMP via find_package; use Homebrew libomp directly
    include_directories(/opt/homebrew/opt/libomp/include)
    link_directories(/opt/homebrew/opt/libomp/lib)
    set(OPENMP_LIBRARIES omp)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xpreprocessor -fopenmp")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Xpreprocessor -fopenmp")
    set(OPENMP_FOUND_VIA "homebrew-libomp")
else()
    message(WARNING "OpenMP not found — parallel loops will run single-threaded. Install libomp (macOS: brew install libomp, Ubuntu: apt install libgomp1).")
endif()

# ──────────────────────────────────────────────────────────────
# ISA extensions for EMP-toolkit headers (x86_64)
# ──────────────────────────────────────────────────────────────
# EMP's x86 headers (aes_opt.h, mitccrh.h, prg.h) call AES-NI / SSSE3 / RDSEED
# intrinsics (_mm_aesenclast_si128, _mm_shuffle_epi8, _rdseed64_step) inside
# always_inline functions. Consumers must enable the same ISA extensions EMP is
# itself built with (see emp_install/cmake/emp-base.cmake: -march=native -maes
# -mrdseed) or GCC aborts with "target specific option mismatch". This surfaces
# only on x86_64; arm64 uses EMP's NEON crypto path and needs none of these.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -march=native -maes -mrdseed")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -maes -mrdseed")
endif()

# ──────────────────────────────────────────────────────────────
# Dependency: OpenSSL (for offline AES blinding)
# ──────────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)

# ──────────────────────────────────────────────────────────────
# Dependency: EMP toolkit (for garbled circuits)
# EMP packages use find_package() internally, so FetchContent
# doesn't work — must use ExternalProject_Add with sequential
# installs into a local prefix.
# ──────────────────────────────────────────────────────────────
include(ExternalProject)
set(EMP_PREFIX ${CMAKE_BINARY_DIR}/emp_install)
set(EMP_INCLUDE_DIR ${EMP_PREFIX}/include)
set(EMP_LIB_DIR ${EMP_PREFIX}/lib)
# Create include dir now so cmake doesn't complain about missing dirs
file(MAKE_DIRECTORY ${EMP_INCLUDE_DIR})

ExternalProject_Add(emp_tool_ext
    GIT_REPOSITORY https://github.com/emp-toolkit/emp-tool.git
    GIT_TAG        0.2.5
    PREFIX         ${CMAKE_BINARY_DIR}/emp_tool_build
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EMP_PREFIX}
        -DCMAKE_PREFIX_PATH=${EMP_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DTHREADING=ON
        -DENABLE_FLOAT=OFF
        -DEMP_TOOL_BUILD_TESTS=OFF
    BUILD_BYPRODUCTS ${EMP_LIB_DIR}/libemp-tool${CMAKE_SHARED_LIBRARY_SUFFIX}
    # Patch: macOS/clang resolves bare 'bind(sock,...)' as std::bind due to <functional>.
    # Use ::bind to force POSIX socket bind in all EMP io headers.
    INSTALL_COMMAND
        ${CMAKE_COMMAND} --build . --target install
        COMMAND bash -c "sed -i.bak 's/if(bind(mysocket/if(::bind(mysocket/g' ${EMP_PREFIX}/include/emp-tool/io/net_io_channel.h || true"
        COMMAND bash -c "sed -i.bak 's/if (bind(mysocket/if (::bind(mysocket/g' ${EMP_PREFIX}/include/emp-tool/io/highspeed_net_io_channel.h || true"
)

ExternalProject_Add(emp_ot_ext
    DEPENDS        emp_tool_ext
    GIT_REPOSITORY https://github.com/emp-toolkit/emp-ot.git
    GIT_TAG        0.2.4
    PREFIX         ${CMAKE_BINARY_DIR}/emp_ot_build
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EMP_PREFIX}
        -DCMAKE_PREFIX_PATH=${EMP_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DEMP_OT_BUILD_TESTS=OFF
        -DCMAKE_CXX_FLAGS=-DTHREADING
)

ExternalProject_Add(emp_sh2pc_ext
    DEPENDS        emp_ot_ext
    GIT_REPOSITORY https://github.com/emp-toolkit/emp-sh2pc.git
    GIT_TAG        0.2.2
    PREFIX         ${CMAKE_BINARY_DIR}/emp_sh2pc_build
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${EMP_PREFIX}
        -DCMAKE_PREFIX_PATH=${EMP_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DEMP_SH2PC_BUILD_TESTS=OFF
        -DCMAKE_CXX_FLAGS=-DTHREADING
)

# IMPORTED target: emp-tool (shared lib + headers)
add_library(emp-tool SHARED IMPORTED GLOBAL)
set_target_properties(emp-tool PROPERTIES
    IMPORTED_LOCATION             ${EMP_LIB_DIR}/libemp-tool${CMAKE_SHARED_LIBRARY_SUFFIX}
    INTERFACE_INCLUDE_DIRECTORIES ${EMP_INCLUDE_DIR}
    INTERFACE_LINK_LIBRARIES      "OpenSSL::Crypto;pthread"
)
add_dependencies(emp-tool emp_tool_ext)

# IMPORTED targets: emp-ot and emp-sh2pc (header-only)
add_library(emp-ot    INTERFACE IMPORTED GLOBAL)
add_library(emp-sh2pc INTERFACE IMPORTED GLOBAL)
set_target_properties(emp-ot    PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${EMP_INCLUDE_DIR})
set_target_properties(emp-sh2pc PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${EMP_INCLUDE_DIR})
add_dependencies(emp-ot    emp_ot_ext)
add_dependencies(emp-sh2pc emp_sh2pc_ext)

# ──────────────────────────────────────────────────────────────
# Global include directories
# ──────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gc
    ${CMAKE_PREFIX_PATH}/include
    /usr/local/include
    /opt/homebrew/include
)

if(nlohmann_json_INCLUDE_DIRS)
    include_directories(${nlohmann_json_INCLUDE_DIRS})
endif()
if(FLINT_INCLUDE_DIRS)
    include_directories(${FLINT_INCLUDE_DIRS})
endif()
if(GMP_INCLUDE_DIRS)
    include_directories(${GMP_INCLUDE_DIRS})
endif()
if(MPFR_INCLUDE_DIRS)
    include_directories(${MPFR_INCLUDE_DIRS})
endif()

if(EXISTS /opt/homebrew/lib)
    link_directories(/opt/homebrew/lib)
endif()

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_modpoly  (C, not C++)
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_modpoly src/protocol/modpoly.c)
set_target_properties(cstpsi_modpoly PROPERTIES LINKER_LANGUAGE C)
target_compile_features(cstpsi_modpoly PRIVATE c_std_11)
target_link_libraries(cstpsi_modpoly ${MATH_LIBRARIES})

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_params
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_params src/protocol/params.cc)
target_link_libraries(cstpsi_params ${SEAL_TARGET})

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_helper
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_helper src/protocol/helper.cc)
target_link_libraries(cstpsi_helper cstpsi_params)

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_plain
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_plain src/protocol/plain.cc)
target_link_libraries(cstpsi_plain cstpsi_helper)
# plaintextPolyEval has an active OpenMP pragma; make the dependency explicit
# (mirrors cstpsi_sender_lib / cstpsi_receiver_lib) so it cannot silently
# de-parallelize if the global OpenMP flags move.
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_plain OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Library: flpsi_wrapper (FLPSI LSH encoding and subsampling)
# ──────────────────────────────────────────────────────────────
add_library(flpsi_wrapper src/flpsi/flpsi_wrapper.cc)
target_compile_options(flpsi_wrapper PRIVATE -std=c++17)

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_receiver_lib (client-side protocol)
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_receiver_lib src/protocol/receiver.cc)
target_compile_options(cstpsi_receiver_lib PRIVATE -std=c++17)
target_link_libraries(cstpsi_receiver_lib cstpsi_helper)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_receiver_lib OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_sender_lib (server-side protocol)
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_sender_lib src/protocol/sender.cc)
target_compile_options(cstpsi_sender_lib PRIVATE -std=c++17)
target_link_libraries(cstpsi_sender_lib cstpsi_plain cstpsi_modpoly)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_sender_lib OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_io  (CSV + param_loader + instrumentation)
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_io
    src/io/csv.cc
    src/io/param_loader.cc
    src/io/instrumentation.cc
)
target_compile_options(cstpsi_io PRIVATE -std=c++17)
target_link_libraries(cstpsi_io cstpsi_helper cstpsi_modpoly)

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_network  (ZMQ + SEAL serialization)
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_network
    src/network/network.cc
    src/network/serialization.cc
)
target_compile_options(cstpsi_network PRIVATE -std=c++17)
target_include_directories(cstpsi_network PUBLIC
    ${ZMQ_INCLUDE_DIRS}
)
if(CPPZMQ_INCLUDE_DIRS)
    target_include_directories(cstpsi_network PUBLIC ${CPPZMQ_INCLUDE_DIRS})
endif()
target_link_libraries(cstpsi_network PUBLIC
    ${ZMQ_LIBRARIES}
    ${SEAL_TARGET}
    cstpsi_params
)

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_core  (shared orchestration, no main())
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_core src/protocol/core.cc)
target_compile_options(cstpsi_core PRIVATE -std=c++17 -DCSTPSI_LIBRARY_BUILD)
target_link_libraries(cstpsi_core
    cstpsi_receiver_lib
    cstpsi_sender_lib
    ${MATH_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_core OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Library: cstpsi_gc  (garbled circuits with AES)
# ──────────────────────────────────────────────────────────────
add_library(cstpsi_gc src/gc/aes_gc.cc)
target_compile_features(cstpsi_gc PUBLIC cxx_std_17)
target_include_directories(cstpsi_gc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol
    ${EMP_INCLUDE_DIR}
)
target_compile_definitions(cstpsi_gc PRIVATE
    EMP_INSTALL_INCLUDE_DIR="${EMP_INCLUDE_DIR}"
    THREADING  # must match how libemp-tool.dylib was compiled (TLS prot_exec/circ_exec)
)
target_link_libraries(cstpsi_gc PUBLIC
    cstpsi_params
    emp-tool emp-ot emp-sh2pc
    OpenSSL::Crypto
    pthread
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    target_compile_options(cstpsi_gc PRIVATE -march=armv8-a+crypto)
endif()

# ──────────────────────────────────────────────────────────────
# Executable: cstpsi_cli  (interactive demo)
# ──────────────────────────────────────────────────────────────
add_executable(cstpsi_cli app/cli.cc)
target_compile_options(cstpsi_cli PRIVATE -std=c++17)
target_link_libraries(cstpsi_cli
    cstpsi_core
    cstpsi_gc
    cstpsi_receiver_lib
    cstpsi_sender_lib
    cstpsi_helper
    cstpsi_modpoly
    cstpsi_io
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_cli OpenMP::OpenMP_CXX)
endif()

# libuuid: macOS ships uuid_generate/uuid_unparse in libSystem (no link needed);
# on Linux they live in libuuid (uuid-dev headers / libuuid1 runtime).
if(NOT APPLE)
    find_library(UUID_LIBRARY NAMES uuid)
    if(NOT UUID_LIBRARY)
        message(FATAL_ERROR "libuuid not found. Install uuid-dev (Ubuntu/Debian).")
    endif()
    target_link_libraries(cstpsi_cli ${UUID_LIBRARY})
endif()

# ──────────────────────────────────────────────────────────────
# Executable: cstpsi_sender  (network server)
# ──────────────────────────────────────────────────────────────
add_executable(cstpsi_sender app/sender.cc)
target_compile_options(cstpsi_sender PRIVATE -std=c++17)
target_link_libraries(cstpsi_sender
    cstpsi_core
    cstpsi_gc
    cstpsi_network
    cstpsi_receiver_lib
    cstpsi_sender_lib
    cstpsi_plain
    cstpsi_helper
    cstpsi_modpoly
    cstpsi_io
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_sender OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Executable: cstpsi_receiver  (network client)
# ──────────────────────────────────────────────────────────────
add_executable(cstpsi_receiver app/receiver.cc)
target_compile_options(cstpsi_receiver PRIVATE -std=c++17)
target_link_libraries(cstpsi_receiver
    cstpsi_core
    cstpsi_gc
    cstpsi_network
    cstpsi_receiver_lib
    cstpsi_sender_lib
    cstpsi_plain
    cstpsi_helper
    cstpsi_modpoly
    cstpsi_io
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(cstpsi_receiver OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Executable: flpsi_experiment (FAR/FRR measurement harness)
# ──────────────────────────────────────────────────────────────
add_executable(flpsi_experiment app/flpsi_experiment.cc)
target_compile_options(flpsi_experiment PRIVATE -std=c++17)
target_link_libraries(flpsi_experiment
    flpsi_wrapper
    cstpsi_core
    cstpsi_receiver_lib
    cstpsi_sender_lib
    cstpsi_plain
    cstpsi_helper
    cstpsi_modpoly
    cstpsi_params
    ${SEAL_TARGET}
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(flpsi_experiment OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Testing infrastructure
# ──────────────────────────────────────────────────────────────
enable_testing()

# ──────────────────────────────────────────────────────────────
# Test: test_csv  (integration)
# ──────────────────────────────────────────────────────────────
add_executable(test_csv tests/integration/test_csv.cc)
target_compile_options(test_csv PRIVATE -std=c++17)
target_link_libraries(test_csv
    cstpsi_io
    cstpsi_helper
    cstpsi_modpoly
    ${MATH_LIBRARIES}
    pthread
)

# ──────────────────────────────────────────────────────────────
# Test: test_param_loader  (unit)
# ──────────────────────────────────────────────────────────────
# test_param_loader defines mock globals itself, so we compile param_loader.cc and
# helper.cc directly to avoid pulling in cstpsi_params (which also defines those globals).
add_executable(test_param_loader
    tests/unit/test_param_loader.cc
    src/io/param_loader.cc
)
target_compile_options(test_param_loader PRIVATE -std=c++17)
target_link_libraries(test_param_loader
    cstpsi_modpoly
    ${MATH_LIBRARIES}
    ${SEAL_TARGET}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_param_loader OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Test: test_flpsi_wrapper (unit, FLPSI LSH encoding/subsampling)
# ──────────────────────────────────────────────────────────────
add_executable(test_flpsi_wrapper tests/unit/test_flpsi_wrapper.cc)
target_compile_options(test_flpsi_wrapper PRIVATE -std=c++17)
target_link_libraries(test_flpsi_wrapper flpsi_wrapper)

# ──────────────────────────────────────────────────────────────
# Test: test_shamir  (unit, k=2 Shamir SS correctness)
# ──────────────────────────────────────────────────────────────
add_executable(test_shamir tests/unit/test_shamir.cc)
target_compile_options(test_shamir PRIVATE -std=c++17)
target_link_libraries(test_shamir
    cstpsi_receiver_lib
    cstpsi_modpoly
    cstpsi_params
    cstpsi_helper
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_shamir OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Test: test_multi_token (unit, multi-token-round FAR mitigation)
# ──────────────────────────────────────────────────────────────
add_executable(test_multi_token tests/unit/test_multi_token.cc)
target_compile_features(test_multi_token PUBLIC cxx_std_17)
target_include_directories(test_multi_token PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol
)
target_link_libraries(test_multi_token cstpsi_receiver_lib cstpsi_params)
# This test signals failure via assert(); keep assertions live regardless of
# CMAKE_BUILD_TYPE so a Release (-DNDEBUG) build cannot turn it into a no-op
# that exits 0 (false green) under CTest.
target_compile_options(test_multi_token PRIVATE -UNDEBUG)

# ──────────────────────────────────────────────────────────────
# Test: test_disable_1gc (unit, FLPSI'21 baseline flag)
# ──────────────────────────────────────────────────────────────
add_executable(test_disable_1gc tests/unit/test_disable_1gc.cc)
target_compile_features(test_disable_1gc PUBLIC cxx_std_17)
target_link_libraries(test_disable_1gc cstpsi_params)

# ──────────────────────────────────────────────────────────────
# Test: test_aes_reduction (unit)
# ──────────────────────────────────────────────────────────────
add_executable(test_aes_reduction tests/unit/test_aes_reduction.cc)
target_compile_features(test_aes_reduction PUBLIC cxx_std_17)
target_include_directories(test_aes_reduction PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol
)
target_link_libraries(test_aes_reduction cstpsi_params)

# ──────────────────────────────────────────────────────────────
# Test: test_gc_roundtrip (unit)
# ──────────────────────────────────────────────────────────────
add_executable(test_gc_roundtrip tests/unit/test_gc_roundtrip.cc)
target_compile_features(test_gc_roundtrip PUBLIC cxx_std_17)
target_link_libraries(test_gc_roundtrip cstpsi_gc cstpsi_params pthread)

# ──────────────────────────────────────────────────────────────
# Test: test_network_framing  (unit, uint32 LE serialization)
# ──────────────────────────────────────────────────────────────
add_executable(test_network_framing tests/unit/test_network_framing.cc)
target_compile_options(test_network_framing PRIVATE -std=c++17)
target_link_libraries(test_network_framing
    cstpsi_network
    cstpsi_params
    ${MATH_LIBRARIES}
    pthread
)

# ──────────────────────────────────────────────────────────────
# Test: test_serialization  (unit, requires GTest)
# ──────────────────────────────────────────────────────────────
find_package(GTest QUIET)
if(GTest_FOUND OR TARGET GTest::GTest)
    add_executable(test_serialization tests/unit/test_serialization.cc)
    target_compile_options(test_serialization PRIVATE -std=c++17)
    target_link_libraries(test_serialization
        cstpsi_network
        cstpsi_helper
        cstpsi_params
        ${MATH_LIBRARIES}
        GTest::GTest
        GTest::Main
    )
    message(STATUS "GTest found - test_serialization will be built")
else()
    message(STATUS "GTest not found - test_serialization will be skipped")
endif()

# NOTE: test_partition_splits and test_padding deferred
# (require full SEAL/sender context initialization with threading that is
#  fragile in unit-test isolation; the 2+2 smoke tests cover full e2e behavior)

# ──────────────────────────────────────────────────────────────
# Test: test_protocol_arithmetic  (unit, core match/reconstruct)
# ──────────────────────────────────────────────────────────────
add_executable(test_protocol_arithmetic tests/unit/test_protocol_arithmetic.cc)
target_compile_options(test_protocol_arithmetic PRIVATE -std=c++17)
target_link_libraries(test_protocol_arithmetic
    cstpsi_receiver_lib
    cstpsi_modpoly
    cstpsi_params
    cstpsi_helper
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_protocol_arithmetic OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Test: test_multi_token_arithmetic  (unit, FAR amplification)
# ──────────────────────────────────────────────────────────────
add_executable(test_multi_token_arithmetic tests/unit/test_multi_token_arithmetic.cc)
target_compile_options(test_multi_token_arithmetic PRIVATE -std=c++17)
target_link_libraries(test_multi_token_arithmetic
    cstpsi_receiver_lib
    cstpsi_modpoly
    cstpsi_params
    cstpsi_helper
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_multi_token_arithmetic OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# Test: test_partition_completeness (regression, partition invariant)
# ──────────────────────────────────────────────────────────────
add_executable(test_partition_completeness tests/unit/test_partition_completeness.cc)
target_compile_options(test_partition_completeness PRIVATE -std=c++17)
target_link_libraries(test_partition_completeness
    cstpsi_sender_lib
    cstpsi_params
    cstpsi_helper
    cstpsi_modpoly
    ${MATH_LIBRARIES}
    ${OPENMP_LIBRARIES}
    pthread
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_partition_completeness OpenMP::OpenMP_CXX)
endif()

# ──────────────────────────────────────────────────────────────
# EMP is a shared lib in a local prefix (no rpath baked in), so tests that link
# it need the loader pointed at it: DYLD_LIBRARY_PATH on macOS, LD_LIBRARY_PATH
# on Linux.
if(APPLE)
    set(EMP_DYLD_LIBRARY_PATH "DYLD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/emp_install/lib")
else()
    set(EMP_DYLD_LIBRARY_PATH "LD_LIBRARY_PATH=${CMAKE_BINARY_DIR}/emp_install/lib")
endif()

# ──────────────────────────────────────────────────────────────
# CTest registration for all test binaries
# ──────────────────────────────────────────────────────────────

# test_csv links only SEAL (system path), not EMP -- no DYLD override needed.
add_test(NAME test_csv COMMAND test_csv)

# test_flpsi_wrapper: FLPSI LSH and subsampling (no special environment)
add_test(NAME test_flpsi_wrapper COMMAND test_flpsi_wrapper)

# test_param_loader reads the relative fixture parameters/demo_1k.json,
# so it must run from the source tree root regardless of where ctest is invoked.
add_test(NAME test_param_loader COMMAND test_param_loader
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME test_shamir COMMAND test_shamir)
add_test(NAME test_multi_token COMMAND test_multi_token)
add_test(NAME test_disable_1gc COMMAND test_disable_1gc)
add_test(NAME test_aes_reduction COMMAND test_aes_reduction)

add_test(NAME test_gc_roundtrip COMMAND test_gc_roundtrip)
if(EMP_DYLD_LIBRARY_PATH)
    set_tests_properties(test_gc_roundtrip PROPERTIES ENVIRONMENT "${EMP_DYLD_LIBRARY_PATH}")
endif()

add_test(NAME test_network_framing COMMAND test_network_framing)

if(TARGET test_serialization)
    add_test(NAME test_serialization COMMAND test_serialization)
endif()

add_test(NAME test_protocol_arithmetic COMMAND test_protocol_arithmetic)
add_test(NAME test_multi_token_arithmetic COMMAND test_multi_token_arithmetic)
add_test(NAME test_partition_completeness COMMAND test_partition_completeness)

# ──────────────────────────────────────────────────────────────
# Installation rules
# ──────────────────────────────────────────────────────────────
install(TARGETS cstpsi_cli cstpsi_sender cstpsi_receiver
    RUNTIME DESTINATION bin
)

install(TARGETS cstpsi_params cstpsi_helper cstpsi_plain cstpsi_receiver_lib
                cstpsi_sender_lib cstpsi_modpoly cstpsi_io cstpsi_network cstpsi_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# ──────────────────────────────────────────────────────────────
# Configuration summary
# ──────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "CSTPSI Configuration Summary:")
message(STATUS "  SEAL: ${SEAL_DIR}")
message(STATUS "  FLINT: ${FLINT_LIBRARIES}")
message(STATUS "  GMP: ${GMP_LIBRARIES}")
message(STATUS "  ZMQ: ${ZMQ_LIBRARIES}")
if(OPENMP_FOUND_VIA)
    message(STATUS "  OpenMP: yes (via ${OPENMP_FOUND_VIA})")
else()
    message(STATUS "  OpenMP: not found (single-threaded fallback)")
endif()
message(STATUS "")
