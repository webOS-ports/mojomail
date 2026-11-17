# @@@LICENSE
#
#      Copyright (c) 2007-2013 LG Electronics, Inc.
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

cmake_minimum_required(VERSION 3.10)

project(mojomail-smtp CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC2)
webos_component(2 0 0)

webos_add_compiler_flags(ALL "-DMOJ_LINUX")
webos_add_compiler_flags(ALL "-O -ggdb -Wuninitialized -Winit-self")

# Sanitizers for the test and hardening harness (tests/hardening/ at the top
# of the repository). Never on for an image build:
#   cmake -DMOJ_SANITIZE=address,undefined ...
set(MOJ_SANITIZE "" CACHE STRING
    "Sanitizers, as -fsanitize= takes them (address,undefined or thread)")
set(MOJ_NO_SANITIZE "" CACHE STRING
    "Checks to leave out of MOJ_SANITIZE, as -fno-sanitize= takes them")
if(MOJ_SANITIZE)
    set(MOJ_SANITIZE_FLAGS -fsanitize=${MOJ_SANITIZE})
    if(MOJ_NO_SANITIZE)
        list(APPEND MOJ_SANITIZE_FLAGS -fno-sanitize=${MOJ_NO_SANITIZE})
    endif()
    add_compile_options(${MOJ_SANITIZE_FLAGS} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    link_libraries(${MOJ_SANITIZE_FLAGS})
endif()

webos_add_linker_options(ALL "--unresolved-symbols=ignore-in-shared-libs -rpath-link ${WEBOS_INSTALL_ROOT}/lib")

include_directories(inc)

# These variables can change the default behaviour and need to be set before calling find_package
set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost REQUIRED COMPONENTS regex system filesystem)
include_directories(Boost_INCLUDE_DIRS)

include(FindPkgConfig)

pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

pkg_check_modules(LUNASERVICE REQUIRED luna-service2)
include_directories(${LUNASERVICE_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${LUNASERVICE_CFLAGS})

pkg_check_modules(JSON json-c)
include_directories(${JSON_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${JSON_CFLAGS})

#pkg_check_modules(MOJODB mojodb)
#include_directories(${MOJODB_INCLUDE_DIRS})
#webos_add_compiler_flags(ALL ${MOJODB_CFLAGS})

pkg_check_modules(EMAILCOMMON email-common)
# Build against the email-common of this tree instead of the installed one:
# its headers from ../common, its library from that component's build
# directory. For working on the whole repository at once, and what the test
# harness does (tests/hardening/).
set(EMAILCOMMON_BUILD_DIR "" CACHE PATH
    "Build directory of ../common to build against instead of the installed email-common")
if(EMAILCOMMON_BUILD_DIR)
    set(EMAILCOMMON_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/../common/include/public/email-common)
    set(EMAILCOMMON_CFLAGS "")
    set(EMAILCOMMON_LDFLAGS -L${EMAILCOMMON_BUILD_DIR} -lemail-common)
endif()
include_directories(${EMAILCOMMON_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${EMAILCOMMON_CFLAGS})

pkg_check_modules(PALMSOCKET REQUIRED palmsocket)
include_directories(${PALMSOCKET_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PALMSOCKET_CFLAGS})

pkg_check_modules(PMSTATEMACHINE REQUIRED PmStateMachineEngine)
include_directories(${PMSTATEMACHINE_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMSTATEMACHINE_CFLAGS})

pkg_check_modules(SANDBOX REQUIRED sandbox)
include_directories(${SANDBOX_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${SANDBOX_CFLAGS})

pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${PMLOG_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PMLOG_CFLAGS})

pkg_check_modules(CARES REQUIRED libcares)
include_directories(${CARES_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${CARES_CFLAGS})

pkg_check_modules(JEMALLOC_MT REQUIRED jemalloc_mt)
include_directories(${JEMALLOC_MT_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${JEMALLOC_MT_CFLAGS})

pkg_check_modules(DB8 REQUIRED db8)
include_directories(${DB8_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${DB8_CFLAGS_OTHER})

find_library(ICU NAMES icuuc)
if(ICU STREQUAL "ICU-NOTFOUND")
   message(FATAL_ERROR "Failed to find ICU4C libraries. Please install.")
endif()

aux_source_directory(src/activity activity_files)
aux_source_directory(src/client client_files)
aux_source_directory(src/commands commands_files)
aux_source_directory(src/data data_files)
aux_source_directory(src/stream stream_files)

# Everything but main(), so the unit tests link the same objects.
add_library(mojomail-smtp-core OBJECT src/SmtpServiceApp.cpp src/SmtpSimpleSender.cpp src/SmtpBusDispatcher.cpp src/SmtpClient.cpp src/SmtpConfig.cpp src/SmtpPowerManager.cpp src/SmtpValidator.cpp ${activity_files} ${client_files} ${commands_files} ${data_files} ${stream_files})

add_executable(mojomail-smtp src/SmtpMain.cpp $<TARGET_OBJECTS:mojomail-smtp-core>)

target_link_libraries(mojomail-smtp ${Boost_LIBRARIES} ${GLIB2_LDFLAGS} ${DB8_LDFLAGS} ${ICU} ${EMAILCOMMON_LDFLAGS} ${PALMSOCKET_LDFLAGS} ${SANDBOX_LDFLAGS} ${JEMALLOC_MT_LDFLAGS} ${PMLOG_LDFLAGS} ${CARES_LDFLAGS} ${PMSTATEMACHINE_LDFLAGS} pthread curl) 

# Unit tests in test/, off unless asked for. See tests/README.md at the top
# of the repository.
option(BUILD_TESTING "Build the unit tests in test/" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

webos_build_program(NAME mojomail-smtp)
webos_build_system_bus_files()
