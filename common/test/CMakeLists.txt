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

# email-common's unit tests: the gtest suites from the original Palm tree,
# with the mocks in test-shared/ (which the services' tests use too), linked
# with the library as built here. See tests/README.md at the top of the
# repository for running them and for the hardening harness around them.

find_package(GTest REQUIRED)

file(GLOB_RECURSE COMMON_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${PROJECT_SOURCE_DIR}/test-shared/*.cpp)

add_executable(email-common-tests ${COMMON_TEST_SOURCES})

target_include_directories(email-common-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/test-shared)

target_link_libraries(email-common-tests
    email-common
    GTest::gtest
    ${GLIB2_LDFLAGS} ${JSON_LDFLAGS} ${PALMSOCKET_LDFLAGS} ${SANDBOX_LDFLAGS}
    ${PMLOG_LDFLAGS} ${LUNASERVICE_LDFLAGS} ${ICU} ${ICUI18N} ${LIBZ}
    ${Boost_LIBRARIES} ${DB8_LDFLAGS} pthread curl)

# Mocks implement whole interfaces and ignore most arguments by design.
target_compile_options(email-common-tests PRIVATE -Wno-unused-parameter)

add_test(NAME email-common-tests COMMAND email-common-tests)
