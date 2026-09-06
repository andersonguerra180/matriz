# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-src")
  file(MAKE_DIRECTORY "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-src")
endif()
file(MAKE_DIRECTORY
  "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-build"
  "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix"
  "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix/tmp"
  "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix/src/sqlite3_amalgamation-populate-stamp"
  "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix/src"
  "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix/src/sqlite3_amalgamation-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix/src/sqlite3_amalgamation-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Volumes/BUNKER 4TB/Apps/BKR Matriz/build-trial/_deps/sqlite3_amalgamation-subbuild/sqlite3_amalgamation-populate-prefix/src/sqlite3_amalgamation-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
