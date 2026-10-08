# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.22)
project(kwallet-icloud-import VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(BUILD_TESTING "Build tests" ON)
option(KWIMPORT_SANITIZE "Build with ASan + UBSan" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
find_package(KF6Wallet REQUIRED)

add_compile_definitions(QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_KEYWORDS
                        KWIMPORT_VERSION="${PROJECT_VERSION}")
add_compile_options(-Wall -Wextra -Wformat=2 -fstack-protector-strong
                    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
add_link_options(-Wl,-z,relro,-z,now)
if(KWIMPORT_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(kwimportcore STATIC src/apple_csv.cpp src/importer.cpp)
target_include_directories(kwimportcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kwimportcore PUBLIC Qt6::Core)

add_executable(kwallet-icloud-import src/main.cpp src/kwallet_folder.cpp)
target_link_libraries(kwallet-icloud-import PRIVATE kwimportcore KF6::Wallet)

include(GNUInstallDirs)
install(TARGETS kwallet-icloud-import DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_import tests/test_import.cpp)
    target_link_libraries(test_import PRIVATE kwimportcore Qt6::Test)
    add_test(NAME test_import COMMAND test_import)
    # tests initialise AppleEntry partially on purpose
    target_compile_options(test_import PRIVATE -Wno-missing-field-initializers)
endif()
