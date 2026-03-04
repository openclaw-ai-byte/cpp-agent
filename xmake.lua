-- xmake.lua - AI Agent Project
set_project("cpp-agent")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

-- Dependencies (only packages available in xmake repos)
add_requires("nlohmann_json")
add_requires("spdlog")
add_requires("sqlite3")
add_requires("boost", {configs = {filesystem = true, system = true, asio = true}})

-- Main executable
target("cpp-agent")
    set_kind("binary")
    
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3")
    
    add_includedirs("src", "include")
    add_files("src/**/*.cpp")
    
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")
    
    -- Use system curl for HTTP (or libcurl if available)
    add_links("curl")
    add_syslinks("pthread", "dl")

-- Library target (optional, for modular build)
target("agent-core")
    set_kind("static")
    
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3")
    
    add_includedirs("include")
    add_files("src/core/**/*.cpp", "src/tools/**/*.cpp", "src/skills/**/*.cpp", "src/mcp/**/*.cpp")
