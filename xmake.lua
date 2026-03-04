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

-- Core library (contains all agent functionality)
target("agent-core")
    set_kind("static")
    
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3", "libcurl")
    add_includedirs("include", {public = true})
    
    add_files("src/core/*.cpp")
    add_files("src/tools/*.cpp")
    add_files("src/skills/*.cpp")
    add_files("src/mcp/*.cpp")
    
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")

-- Main executable
target("cpp-agent")
    set_kind("binary")
    
    add_deps("agent-core")
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3", "libcurl")
    add_includedirs("include", {public = true})
    
    add_files("src/main.cpp")
    
    -- System libraries
    if is_plat("linux") then
        add_syslinks("pthread", "dl", "stdc++fs")
    elseif is_plat("macosx", "windows") then
        add_syslinks("pthread", "dl")
    end
