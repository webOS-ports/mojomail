# @@@LICENSE
#
#      Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

# The build host's own gcc or clang, compiling against an x86-64 recipe
# sysroot. The image toolchain ships no sanitizer runtimes; the host's do,
# and with the same glibc as the sysroot they run against its libraries.
#
#   MOJ_SYSROOT   <recipe workdir>/recipe-sysroot of the corei7-64 build
#   MOJ_HOST_CC   gcc or clang (default gcc)

# try_compile() re-reads this file in a project of its own.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MOJ_SYSROOT MOJ_HOST_CC)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if (NOT MOJ_HOST_CC)
    set(MOJ_HOST_CC gcc)
endif()
if (MOJ_HOST_CC STREQUAL "clang")
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
else()
    set(CMAKE_C_COMPILER gcc)
    set(CMAKE_CXX_COMPILER g++)
endif()

set(CMAKE_SYSROOT ${MOJ_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${MOJ_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config is pointed at the sysroot by the caller's environment
# (PKG_CONFIG_SYSROOT_DIR, PKG_CONFIG_LIBDIR): see lib.sh.
