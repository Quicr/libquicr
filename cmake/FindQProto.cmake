# - Try to find Quicrq

if (quicr_GIT_SUBMODULE)
    set(QPROTO_LIBRARIES quicrq-core)
    set(QPROTO_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/contrib/qproto/include)
    message(STATUS "huahhaaaha ${QPROTO_INCLUDE_DIRS}")
else(quicr_GIT_SUBMODULE)
    find_path(QUICRQ_INCLUDE_DIR
            NAMES quicrq.h
            HINTS ${CMAKE_SOURCE_DIR}/../quicrq/include
            ${CMAKE_BINARY_DIR}/../quicrq/include
            ../quicrq/include )

    find_path(QUICRQ_INTERNAL_DIR
            NAMES quicrq_internal.h
            HINTS ${CMAKE_SOURCE_DIR}/../quicrq/lib
            ${CMAKE_BINARY_DIR}/../quicrq/lib
            ../quicrq/lib )

    set(QUICRQ_HINTS ${CMAKE_BINARY_DIR}/../quicrq ../quicrq)

    find_library(QUICRQ_CORE_LIBRARY quicrq-core HINTS ${QUICRQ_HINTS})

    include(FindPackageHandleStandardArgs)
    # handle the QUIETLY and REQUIRED arguments and set PTLS_FOUND to TRUE
    # if all listed variables are TRUE
    find_package_handle_standard_args(Quicr REQUIRED_VARS
        QUICRQ_CORE_LIBRARY
        QUICRQ_INCLUDE_DIR
        QUICRQ_INTERNAL_DIR)

    if(Quicr_FOUND)
        set(QPROTO_LIBRARIES
            ${QUICRQ_CORE_LIBRARY})
        set(QPROTO_INCLUDE_DIRS ${QUICRQ_INCLUDE_DIR} ${QUICRQ_INTERNAL_DIR})
    endif()
endif(quicr_GIT_SUBMODULE)

mark_as_advanced(QPROTO_LIBRARIES QPROTO_INCLUDE_DIRS)
