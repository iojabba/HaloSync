cmake_minimum_required(VERSION 3.21)
project(HaloSync VERSION 0.2.0 LANGUAGES CXX)

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

# FFmpeg libraries for the gapless decoder: pkg-config on Linux/macOS; on
# Windows point FFMPEG_ROOT at a "shared" FFmpeg build (include/ and lib/).
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg dev folder with include/ and lib/ (Windows)")
if(FFMPEG_ROOT)
    add_library(halo_ffmpeg INTERFACE)
    target_include_directories(halo_ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(lib avformat avcodec swresample avutil)
        find_library(FFMPEG_${lib}_LIB NAMES ${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(halo_ffmpeg INTERFACE ${FFMPEG_${lib}_LIB})
    endforeach()
    set(HALO_FFMPEG halo_ffmpeg)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
    set(HALO_FFMPEG PkgConfig::FFMPEG)
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
    src/core/Decoder.h          src/core/Decoder.cpp
    src/core/Loudness.h         src/core/Loudness.cpp
    src/core/ReplayGainJob.h    src/core/ReplayGainJob.cpp
    src/core/Lyrics.h           src/core/Lyrics.cpp
    src/core/ArtFinder.h        src/core/ArtFinder.cpp
    src/core/Radio.h            src/core/Radio.cpp
    src/core/Discover.h         src/core/Discover.cpp
)
target_include_directories(halocore PUBLIC src/core)
if(WIN32)
    target_link_libraries(halocore PRIVATE setupapi)
elseif(APPLE)
    target_link_libraries(halocore PRIVATE "-framework IOKit" "-framework CoreFoundation")
endif()
target_link_libraries(halocore PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network Qt6::Concurrent ${HALO_TAGLIB} ${HALO_FFMPEG})

# ---------------------------------------------------------------- application
set(APP_SOURCES
    src/app/main.cpp
    src/app/Theme.h             src/app/Theme.cpp
    src/app/AudioEngine.h       src/app/AudioEngine.cpp
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
    src/app/LyricsPanel.h       src/app/LyricsPanel.cpp
    src/app/RadioView.h         src/app/RadioView.cpp
    src/app/DiscoverView.h      src/app/DiscoverView.cpp
    src/app/DuplicateDialog.h   src/app/DuplicateDialog.cpp
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
    foreach(t test_autoplaylist test_playlistio test_library test_sync test_decoder test_online test_features)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE halocore Qt6::Test)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
