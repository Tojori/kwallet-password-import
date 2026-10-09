# SPDX-License-Identifier: LGPL-2.1-or-later
cmake_minimum_required(VERSION 3.22)
project(kwallet-password-import VERSION 0.2.0 LANGUAGES CXX)

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

add_library(kwimportcore STATIC src/csv.cpp src/formats.cpp src/importer.cpp src/kwallet_folder.cpp src/secure_delete.cpp)
target_include_directories(kwimportcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kwimportcore PUBLIC Qt6::Core KF6::Wallet)

add_executable(kwallet-password-import src/main.cpp)
target_link_libraries(kwallet-password-import PRIVATE kwimportcore)

include(GNUInstallDirs)
install(TARGETS kwallet-password-import DESTINATION ${CMAKE_INSTALL_BINDIR})

option(KWIMPORT_BUILD_GUI "Build the graphical import wizard" ON)
if(KWIMPORT_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    find_package(KF6CoreAddons REQUIRED)
    find_package(KF6I18n REQUIRED)
    find_package(KF6WidgetsAddons REQUIRED)
    add_executable(kwallet-password-import-gui src/gui/main.cpp src/gui/import_wizard.cpp)
    target_compile_definitions(kwallet-password-import-gui PRIVATE TRANSLATION_DOMAIN="kwallet-password-import")
    target_link_libraries(kwallet-password-import-gui PRIVATE kwimportcore Qt6::Widgets KF6::CoreAddons KF6::I18n KF6::WidgetsAddons)
    install(TARGETS kwallet-password-import-gui DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES data/io.github.tojori.kwalletpasswordimport.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    # Dolphin: right-click on a CSV/JSON export -> "Import passwords into KDE Wallet…"
    install(FILES data/kwallet-password-import-servicemenu.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    ki18n_install(po)
endif()

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(test_import tests/test_import.cpp)
    target_link_libraries(test_import PRIVATE kwimportcore Qt6::Test)
    add_test(NAME test_import COMMAND test_import)
    # tests initialise AppleEntry partially on purpose
    target_compile_options(test_import PRIVATE -Wno-missing-field-initializers)
endif()
