cmake_minimum_required(VERSION 3.20)

project(mmdemo LANGUAGES CXX)

# ---- Core game library (no network dependencies) ----
add_library(mmdemo_core
    src/battle/Battle.cpp
    src/core/ConfigJson.cpp
    src/game/GameServer.cpp
    src/net/SessionCommand.cpp
    src/net/GameSessionHandler.cpp
)

target_compile_features(mmdemo_core PUBLIC cxx_std_17)
target_include_directories(mmdemo_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
)

# ---- Network transport library (Asio + factory) ----
add_library(mmdemo_net
    src/net/AsioClientTransport.cpp
    src/net/AsioServerTransport.cpp
    src/net/TransportFactory.cpp
)

target_compile_features(mmdemo_net PUBLIC cxx_std_17)
target_include_directories(mmdemo_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/asio-1.36.0/include
)
if (WIN32)
    target_link_libraries(mmdemo_net PUBLIC ws2_32)
else()
    target_link_libraries(mmdemo_net PUBLIC pthread)
endif()

# ---- Local command-line version (no network) ----
add_executable(mmdemo_cpp
    src/main.cpp
)

target_link_libraries(mmdemo_cpp PRIVATE mmdemo_core)

# ---- TCP server (network transport + game session) ----
add_executable(mmdemo_server
    src/net_server.cpp
)

target_link_libraries(mmdemo_server PRIVATE mmdemo_core mmdemo_net)

# ---- TCP client (network transport only, no game logic) ----
add_executable(mmdemo_client
    src/net_client.cpp
)

target_compile_features(mmdemo_client PRIVATE cxx_std_17)
target_link_libraries(mmdemo_client PRIVATE mmdemo_net)

# ---- Automated tests ----
add_executable(mmdemo_tests
    src/tests.cpp
)

target_link_libraries(mmdemo_tests PRIVATE mmdemo_core)

if (MSVC)
    target_compile_options(mmdemo_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(mmdemo_cpp PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(mmdemo_server PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(mmdemo_client PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(mmdemo_tests PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(mmdemo_core PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(mmdemo_cpp PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(mmdemo_server PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(mmdemo_client PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(mmdemo_tests PRIVATE -Wall -Wextra -pedantic)
endif()
