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

### Quiche begin

add_custom_target(quiche_binaries ALL
    COMMENT Building Quiche library
    COMMAND cargo build --release --features ffi
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/quiche/)

add_library(quiche_library INTERFACE)

target_include_directories(quiche_library
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/quiche/quiche/include)

target_link_directories(quiche_library
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/quiche/target/release/)

target_link_libraries(quiche_library INTERFACE quiche)

add_dependencies(quiche_library quiche_binaries)

### Quiche end
