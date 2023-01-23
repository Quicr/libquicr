# Enable fetching content
include(FetchContent)

# Transport
message(STATUS "Retrieving transport")
FetchContent_Declare(transport
        GIT_REPOSITORY  git@github.com:Quicr/transport.git
        GIT_TAG         main
        )

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
FetchContent_MakeAvailable(transport)
