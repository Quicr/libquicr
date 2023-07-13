if(BUILD_TESTING AND QUICR_BUILD_TESTS)
    add_subdirectory( doctest )
endif()

add_subdirectory( transport )
add_subdirectory( qname )

# Cantina
set(cantina_BUILD_LOGGER ON CACHE BOOL "Build Logger Module")
set(cantina_BUILD_MEMMGR ON CACHE BOOL "Build Memory Manager Module")
set(cantina_BUILD_NETWORKING ON CACHE BOOL "Build Networking Module")
set(cantina_BUILD_THREADING ON CACHE BOOL "Build Threading Module")
add_subdirectory(cantina)

### Quiche Begin

# Utilize the BoringSSL from qtransport
set(QUICHE_BSSL_PATH ${CMAKE_CURRENT_BINARY_DIR}/transport/dependencies/boringssl)

# Set Environment variables for cargo command.

set(CARGO_CMD QUICHE_BSSL_PATH=${QUICHE_BSSL_PATH})
if (CMAKE_FRAMEWORK)
set(CARGO_CMD ${CARGO_CMD} QUICHE_BSSL_LINK_KIND=framework)
endif()

# Set the actual cargo command to run.

set(CARGO_CMD ${CARGO_CMD} cargo +m10x build -Z build-std --release --lib --features ffi)

if (QUICHE_TARGET)
set(CARGO_CMD ${CARGO_CMD} --target ${QUICHE_TARGET})
endif()

set(CARGO_TARGET_DIR ${CMAKE_CURRENT_BINARY_DIR}/quiche)
set(CARGO_CMD ${CARGO_CMD} --target-dir ${CARGO_TARGET_DIR})

add_custom_target(quiche_target ALL
    COMMENT Building Quiche library
    COMMAND ${CARGO_CMD}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/quiche/)
add_dependencies(quiche_target crypto ssl quicr-transport)

add_library(quiche_library INTERFACE)
add_dependencies(quiche_library quiche_target)
target_include_directories(quiche_library
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/quiche/quiche/include)
target_link_directories(quiche_library
    INTERFACE
        ${CARGO_TARGET_DIR}/${QUICHE_TARGET}/release)
target_link_libraries(quiche_library INTERFACE quiche)

# ### Quiche End
