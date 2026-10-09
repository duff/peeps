cmake_minimum_required(VERSION 3.21)
project(contacts LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)

add_compile_options(-Wall -Wextra -Wpedantic)

# Sources shared by the app and the tests.
qt_add_library(contacts_core STATIC
  src/blockingnam.cpp src/blockingnam.h
  src/cli.cpp src/cli.h
  src/contact.cpp src/contact.h
  src/desktop.cpp src/desktop.h
  src/dialogs.cpp src/dialogs.h
  src/google.cpp src/google.h
  src/http.h
  src/instance.cpp src/instance.h
  src/mainwindow.cpp src/mainwindow.h
  src/net.h
  src/oauth.cpp src/oauth.h
  src/photos.cpp src/photos.h
  src/search.cpp src/search.h
  src/secrets.cpp src/secrets.h
  src/shortcuts.cpp src/shortcuts.h
  src/store.cpp src/store.h
  src/theme.cpp src/theme.h
  src/vcard.cpp src/vcard.h
  src/version.h
  src/worker.h
)
target_include_directories(contacts_core PUBLIC src)
target_link_libraries(contacts_core
  PUBLIC Qt6::Widgets Qt6::Network
  PRIVATE PkgConfig::LIBSECRET
)

# A packaged build can ship a published Google OAuth client so people only
# sign in. Leave these unset and each person brings their own client id.
foreach(name CONTACTS_GOOGLE_CLIENT_ID CONTACTS_GOOGLE_CLIENT_SECRET)
  if(NOT "$ENV{${name}}" STREQUAL "")
    target_compile_definitions(contacts_core PRIVATE "${name}=\"$ENV{${name}}\"")
  endif()
endforeach()

qt_add_executable(contacts src/main.cpp)
target_link_libraries(contacts PRIVATE contacts_core)

install(TARGETS contacts)
install(FILES dist/org.omelia.Contacts.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES dist/org.omelia.Contacts.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  qt_add_executable(contacts-tests
    tests/main.cpp tests/tests.cpp tests/cli_tests.cpp tests/tests.h)
  target_link_libraries(contacts-tests PRIVATE contacts_core Qt6::Test)
  add_test(NAME contacts-tests COMMAND contacts-tests)
  set_tests_properties(contacts-tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
endif()
