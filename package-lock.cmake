# CPM Package Lock
# This file should be committed to version control

# fmt
CPMDeclarePackage(fmt
  GIT_TAG 12.2.0
  GITHUB_REPOSITORY fmtlib/fmt
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# timeq
CPMDeclarePackage(timeq
  GIT_TAG 898d45c
  GITHUB_REPOSITORY quicr/timeq
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# picotls (unversioned)
# CPMDeclarePackage(picotls
#  GIT_TAG sync-092226
#  GITHUB_REPOSITORY quicr/picotls
#  SYSTEM YES
#  EXCLUDE_FROM_ALL YES
#  OPTIONS
#    "WITH_FUSION OFF"
#    "picotls_BUILD_TESTS OFF"
#)
# picoquic (unversioned)
# CPMDeclarePackage(picoquic
#  GIT_TAG poll-recvmmsg
#  GITHUB_REPOSITORY private-octopus/picoquic
#  SYSTEM YES
#  EXCLUDE_FROM_ALL YES
#  OPTIONS
#    "BUILD_DEMO OFF"
#    "BUILD_PQBENCH OFF"
#    "BUILD_LOGREADER OFF"
#    "picoquic_BUILD_TESTS OFF"
#    "PICOQUIC_FETCH_PTLS ON"
#)
# doctest
CPMDeclarePackage(doctest
  VERSION 2.5.2
  GITHUB_REPOSITORY doctest/doctest
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# spdlog
CPMDeclarePackage(spdlog
  VERSION 1.17.0
  GITHUB_REPOSITORY gabime/spdlog
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# benchmark
CPMDeclarePackage(benchmark
  VERSION 1.9.5
  GITHUB_REPOSITORY google/benchmark
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# cxxopts
CPMDeclarePackage(cxxopts
  VERSION 3.3.1
  GITHUB_REPOSITORY jarro2783/cxxopts
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# json
CPMDeclarePackage(json
  VERSION 3.11.3
  GITHUB_REPOSITORY nlohmann/json
  SYSTEM YES
  EXCLUDE_FROM_ALL YES
)
# CPMLicenses
CPMDeclarePackage(CPMLicenses
  NAME CPMLicenses
  VERSION 0.0.7
  GIT_TAG ca42334d561b83e499b11cf55fe05d97a0767fb9
  GITHUB_REPOSITORY cpm-cmake/CPMLicenses.cmake
  EXCLUDE_FROM_ALL YES
)
