cmake_minimum_required(VERSION 3.21)

project(NoPublicShouts VERSION 1.0.2 LANGUAGES CXX)

# CommonLibSSE exports link-interface targets that are not imported by its
# generated config automatically, so load them before CommonLibSSE.
find_package(spdlog CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    NAME "No Public Shouts - False Dragonborn RP"
    AUTHOR "HDLan1202"
    VERSION ${PROJECT_VERSION}
    SOURCES
        src/PCH.h
        src/main.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
