-- xmake.lua - AI Agent Project
set_project("cpp-agent")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

-- Dependencies
add_requires("oatpp", "oatpp-swagger", "oatpp-curl")
add_requires("boost", {configs = {all = true}})
add_requires("nlohmann_json")
add_requires("spdlog")
add_requires("sqlite3")

-- Main executable
target("cpp-agent")
    set_kind("binary")
    
    add_packages("oatpp", "oatpp-swagger", "oatpp-curl")
    add_packages("boost")
    add_packages("nlohmann_json", "spdlog", "sqlite3")
    
    add_includedirs("src", "include")
    add_files("src/**/*.cpp")
    
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")

-- Library target
target("agent-core")
    set_kind("static")
    
    add_packages("oatpp", "boost", "nlohmann_json", "spdlog", "sqlite3")
    
    add_includedirs("include")
    add_files("src/core/**/*.cpp", "src/tools/**/*.cpp", "src/skills/**/*.cpp")
