cmake_minimum_required(VERSION 3.21)

project(ThorinDragonSoulVisuals VERSION 0.2.0 LANGUAGES CXX)

find_package(spdlog CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "Thorin Dragon Soul Visuals"
    AUTHOR "HDLan1202"
    VERSION ${PROJECT_VERSION}
    SOURCES
        src/PCH.h
        src/main.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
