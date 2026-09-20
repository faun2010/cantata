cmake_minimum_required(VERSION 3.16)
project(CantataTranslationServiceTests LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Test)

add_executable(translationservice_test
    translationservice_test.cpp
    ../network/translationservice.cpp
)

add_executable(ollama_translation_smoke
    ollama_translation_smoke.cpp
    ../network/translationservice.cpp
)
target_include_directories(ollama_translation_smoke PRIVATE ..)
target_link_libraries(ollama_translation_smoke PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)
target_include_directories(translationservice_test PRIVATE ..)
target_link_libraries(translationservice_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Test
)

enable_testing()
add_test(NAME translationservice_test COMMAND translationservice_test)

add_executable(artistimageprovider_test
    artistimageprovider_test.cpp
)
target_include_directories(artistimageprovider_test PRIVATE ..)
target_link_libraries(artistimageprovider_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME artistimageprovider_test COMMAND artistimageprovider_test)

add_executable(wikitemplateutils_test
    wikitemplateutils_test.cpp
)
target_include_directories(wikitemplateutils_test PRIVATE ..)
target_link_libraries(wikitemplateutils_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME wikitemplateutils_test COMMAND wikitemplateutils_test)

add_executable(searchterms_test
    searchterms_test.cpp
)
target_include_directories(searchterms_test PRIVATE ..)
target_link_libraries(searchterms_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME searchterms_test COMMAND searchterms_test)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Gui)
add_executable(artworkquality_test artworkquality_test.cpp)
target_include_directories(artworkquality_test PRIVATE ..)
target_link_libraries(artworkquality_test PRIVATE Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME artworkquality_test COMMAND artworkquality_test)

add_executable(discogsartwork_test discogsartwork_test.cpp)
target_include_directories(discogsartwork_test PRIVATE ..)
target_link_libraries(discogsartwork_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME discogsartwork_test COMMAND discogsartwork_test)

add_executable(musictooltip_test musictooltip_test.cpp)
target_include_directories(musictooltip_test PRIVATE ..)
target_link_libraries(musictooltip_test PRIVATE Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME musictooltip_test COMMAND musictooltip_test)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Sql)
add_executable(musicsearch_test musicsearch_test.cpp ../network/musicsearch.cpp ../network/translationservice.cpp)
target_include_directories(musicsearch_test PRIVATE ..)
target_link_libraries(musicsearch_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Sql)
add_test(NAME musicsearch_test COMMAND musicsearch_test)

add_executable(biographytranslation_test biographytranslation_test.cpp)
target_include_directories(biographytranslation_test PRIVATE ..)
target_link_libraries(biographytranslation_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME biographytranslation_test COMMAND biographytranslation_test)

add_executable(workinfo_test workinfo_test.cpp ../context/workinfo.cpp ../context/composertable.cpp)
target_include_directories(workinfo_test PRIVATE ..)
target_link_libraries(workinfo_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME workinfo_test COMMAND workinfo_test)

add_executable(smartfilter_test smartfilter_test.cpp ../playlists/smartfilter.cpp)
target_include_directories(smartfilter_test PRIVATE ..)
target_link_libraries(smartfilter_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME smartfilter_test COMMAND smartfilter_test)

add_executable(composertable_test composertable_test.cpp ../context/composertable.cpp)
target_include_directories(composertable_test PRIVATE ..)
target_link_libraries(composertable_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME composertable_test COMMAND composertable_test)

add_executable(artistlookup_test artistlookup_test.cpp ../context/composertable.cpp)
target_include_directories(artistlookup_test PRIVATE ..)
target_link_libraries(artistlookup_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Test)
add_test(NAME artistlookup_test COMMAND artistlookup_test)

add_executable(recommendedrecordings_test recommendedrecordings_test.cpp ../context/recommendedrecordings.cpp ../context/workinfo.cpp ../context/composertable.cpp)
target_include_directories(recommendedrecordings_test PRIVATE ..)
target_link_libraries(recommendedrecordings_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME recommendedrecordings_test COMMAND recommendedrecordings_test)

add_executable(workdossier_test workdossier_test.cpp ../context/workdossier.cpp ../context/recommendedrecordings.cpp ../context/workinfo.cpp ../context/composertable.cpp)
target_include_directories(workdossier_test PRIVATE ..)
target_link_libraries(workdossier_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME workdossier_test COMMAND workdossier_test)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network)
add_executable(recordingcovers_test recordingcovers_test.cpp ../context/recordingcovers.cpp)
target_include_directories(recordingcovers_test PRIVATE ..)
target_link_libraries(recordingcovers_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME recordingcovers_test COMMAND recordingcovers_test)

add_executable(composerday_test composerday_test.cpp ../playlists/composerday.cpp ../context/recommendedrecordings.cpp ../context/workinfo.cpp ../context/composertable.cpp)
target_include_directories(composerday_test PRIVATE ..)
target_compile_definitions(composerday_test PRIVATE CANTATA_CALENDAR="${CMAKE_CURRENT_SOURCE_DIR}/../playlists/composercalendar.json")
target_link_libraries(composerday_test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)
add_test(NAME composerday_test COMMAND composerday_test)
