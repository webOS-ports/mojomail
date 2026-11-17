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

# mojomail-smtp's unit tests: the gtest suites from the original Palm tree, built
# from the service's own objects (mojomail-smtp-core, everything but main()) and
# email-common's shared test mocks. See tests/README.md at the top of the
# repository for running them and for the hardening harness around them.

find_package(GTest REQUIRED)

# The mocks every service's tests share live with email-common.
set(COMMON_TEST_SHARED ${PROJECT_SOURCE_DIR}/../common/test-shared)

file(GLOB_RECURSE smtp_test_sources CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${COMMON_TEST_SHARED}/*.cpp)

add_executable(mojomail-smtp-tests ${smtp_test_sources} $<TARGET_OBJECTS:mojomail-smtp-core>)

target_include_directories(mojomail-smtp-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${COMMON_TEST_SHARED})

target_link_libraries(mojomail-smtp-tests GTest::gtest ${Boost_LIBRARIES} ${GLIB2_LDFLAGS} ${DB8_LDFLAGS} ${ICU} ${EMAILCOMMON_LDFLAGS} ${PALMSOCKET_LDFLAGS} ${SANDBOX_LDFLAGS} ${JEMALLOC_MT_LDFLAGS} ${PMLOG_LDFLAGS} ${CARES_LDFLAGS} ${PMSTATEMACHINE_LDFLAGS} pthread curl)

# Mocks implement whole interfaces and ignore most arguments by design.
target_compile_options(mojomail-smtp-tests PRIVATE -Wno-unused-parameter)

add_test(NAME mojomail-smtp-tests COMMAND mojomail-smtp-tests)
