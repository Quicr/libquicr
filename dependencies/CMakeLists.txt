# Enable fetching content
include(FetchContent)

# Fetch the picoquic library, which brings in picotls with it
if(IS_DIRECTORY "/ws/picoquic")
    message(STATUS "Using picoquic from /ws/picoquic")
    FetchContent_Declare(picoquic
        URL             /ws/picoquic
    )
else()
    message(STATUS "Retrieving picoquic")
    FetchContent_Declare(picoquic
        GIT_REPOSITORY  https://github.com/private-octopus/picoquic.git
        GIT_TAG         master
    )
endif()

# Set the option to force picoquic to fetch picotls
set(PICOQUIC_FETCH_PTLS ON)

# Fetch the quicrq library
if(IS_DIRECTORY "/ws/quicrq")
    message(STATUS "Using quicrq from /ws/quicrq")
    FetchContent_Declare(quicrq
        URL             /ws/quicrq
    )
else()
    message(STATUS "Retrieving quicrq")
    FetchContent_Declare(quicrq
        GIT_REPOSITORY  git@github.com:Quicr/quicrq.git
        GIT_TAG         main
    )
endif()

# DocTest for testing
if(quicr_BUILD_TESTS)
    find_package(doctest QUIET)
    if(NOT doctest_FOUND)
        message(STATUS "Retrieving doctest")

        # Fetch the doctest library
        FetchContent_Declare(doctest
            GIT_REPOSITORY  https://github.com/doctest/doctest.git
            GIT_TAG         v2.4.9
        )

        # Make dependencies available
        FetchContent_MakeAvailable(DocTest)
    endif()
endif()

# Make dependencies available
FetchContent_MakeAvailable(picoquic quicrq)
