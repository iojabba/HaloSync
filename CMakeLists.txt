cmake_minimum_required(VERSION 3.21)
project(HaloSync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(HALO_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Sql Multimedia Network Concurrent)
find_package(Qt6 6.4 QUIET COMPONENTS DBus Test)
# TagLib: CMake package (TagLib 2.x, vcpkg) or pkg-config (Linux distros, Homebrew).
find_package(TagLib 1.11 CONFIG QUIET)
if(TARGET TagLib::tag)
    set(HALO_TAGLIB TagLib::tag)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib)
    set(HALO_TAGLIB PkgConfig::TAGLIB)
endif()

# ---------------------------------------------------------------- core library
add_library(halocore STATIC
    src/core/Track.h
    src/core/Paths.h            src/core/Paths.cpp
    src/core/Database.h         src/core/Database.cpp
    src/core/TagIO.h            src/core/TagIO.cpp
    src/core/Library.h          src/core/Library.cpp
    src/core/AutoPlaylist.h     src/core/AutoPlaylist.cpp
    src/core/PlaylistIO.h       src/core/PlaylistIO.cpp
    src/core/Transcoder.h       src/core/Transcoder.cpp
    src/core/SyncEngine.h       src/core/SyncEngine.cpp
    src/core/DeviceDetect.h     src/core/DeviceDetect.cpp
    src/core/Reports.h          src/core/Reports.cpp
    src/core/MusicBrainz.h      src/core/MusicBrainz.cpp
)
target_include_directories(halocore PUBLIC src/core)
target_link_libraries(halocore PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network Qt6::Concurrent ${HALO_TAGLIB})

# ---------------------------------------------------------------- application
set(APP_SOURCES
    src/app/main.cpp
    src/app/Theme.h             src/app/Theme.cpp
    src/app/Player.h            src/app/Player.cpp
    src/app/TrackModel.h        src/app/TrackModel.cpp
    src/app/MainWindow.h        src/app/MainWindow.cpp
    src/app/CollectionPage.h    src/app/CollectionPage.cpp
    src/app/QuickplayPage.h     src/app/QuickplayPage.cpp
    src/app/SyncPage.h          src/app/SyncPage.cpp
    src/app/SettingsPage.h      src/app/SettingsPage.cpp
    src/app/TagEditorDialog.h   src/app/TagEditorDialog.cpp
    src/app/AutoPlaylistDialog.h src/app/AutoPlaylistDialog.cpp
    src/app/AlbumLookupDialog.h src/app/AlbumLookupDialog.cpp
    src/app/MiniPlayer.h        src/app/MiniPlayer.cpp
    src/app/MergeList.h         src/app/MergeList.cpp
)
if(Qt6DBus_FOUND AND UNIX AND NOT APPLE)
    list(APPEND APP_SOURCES src/app/Mpris.h src/app/Mpris.cpp)
endif()

qt_add_executable(halo-sync ${APP_SOURCES})
target_link_libraries(halo-sync PRIVATE halocore Qt6::Widgets Qt6::Multimedia)
target_compile_definitions(halo-sync PRIVATE HALO_VERSION_STRING="${PROJECT_VERSION}")
if(Qt6DBus_FOUND AND UNIX AND NOT APPLE)
    target_link_libraries(halo-sync PRIVATE Qt6::DBus)
    target_compile_definitions(halo-sync PRIVATE HALO_HAVE_MPRIS=1)
endif()
set_target_properties(halo-sync PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
install(TARGETS halo-sync RUNTIME DESTINATION bin BUNDLE DESTINATION .)

# ---------------------------------------------------------------- tests
if(HALO_BUILD_TESTS AND Qt6Test_FOUND)
    enable_testing()
    foreach(t test_autoplaylist test_playlistio test_library test_sync)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE halocore Qt6::Test)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
