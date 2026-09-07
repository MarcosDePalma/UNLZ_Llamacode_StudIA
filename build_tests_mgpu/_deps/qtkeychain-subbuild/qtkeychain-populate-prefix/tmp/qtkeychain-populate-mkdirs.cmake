# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-src")
  file(MAKE_DIRECTORY "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-build"
  "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix"
  "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix/tmp"
  "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix/src/qtkeychain-populate-stamp"
  "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix/src"
  "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix/src/qtkeychain-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix/src/qtkeychain-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/cristian/Documents/LlamaCode/build_tests_mgpu/_deps/qtkeychain-subbuild/qtkeychain-populate-prefix/src/qtkeychain-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
