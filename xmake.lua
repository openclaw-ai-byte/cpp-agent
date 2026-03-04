-- xmake.lua - AI Agent Project
set_project("cpp-agent")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

-- Dependencies
add_requires("nlohmann_json")
add_requires("spdlog")
add_requires("sqlite3")
add_requires("boost", {configs = {filesystem = true, system = true, asio = true}})
add_requires("libcurl")

-- Main executable
target("cpp-agent")
    set_kind("binary")
    
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3", "libcurl")
    
    -- Include headers from include/ directory
    add_includedirs("include", {public = true})
    add_files("src/**/*.cpp")
    
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")
    add_syslinks("pthread", "dl")

-- Library target (optional, for modular build)
target("agent-core")
    set_kind("static")
    
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3", "libcurl")
    
    add_includedirs("include", {public = true})
    add_files("src/core/**/*.cpp", "src/tools/**/*.cpp", "src/skills/**/*.cpp", "src/mcp/**/*.cpp")
