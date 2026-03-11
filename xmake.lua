-- xmake.lua - AI Agent Project
set_project("cpp-agent")
set_version("0.1.0")
set_languages("c++23")

add_rules("mode.debug", "mode.release")

-- Dependencies
add_requires("nlohmann_json")
add_requires("spdlog")
add_requires("sqlite3")
add_requires("boost", {configs = {filesystem = true, system = true, asio = true, context = true, fiber = true}})
add_requires("libcurl")
add_requires("yaml-cpp")

-- Core library (contains all agent functionality)
target("agent-core")
    set_kind("static")
    
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3", "libcurl", "yaml-cpp")
    add_includedirs("include", {public = true})
    
    add_files("src/core/*.cpp")
    add_files("src/tools/*.cpp")
    add_files("src/skills/*.cpp")
    add_files("src/mcp/*.cpp")
    add_files("src/cron/*.cpp")
    add_files("src/memory/*.cpp")
    
    add_cxxflags("-Wall", "-Wextra", "-Wpedantic")

-- Main executable
target("cpp-agent")
    set_kind("binary")
    
    add_deps("agent-core")
    add_packages("boost", "nlohmann_json", "spdlog", "sqlite3", "libcurl", "yaml-cpp")
    add_includedirs("include", {public = true})
    
    add_files("src/main.cpp")
    
    -- System libraries
    if is_plat("linux") then
        add_syslinks("pthread", "dl", "stdc++fs")
    elseif is_plat("macosx") then
        add_syslinks("pthread", "dl")
    elseif is_plat("windows") then
        add_syslinks("ws2_32", "wsock32")
    end

-- Unit tests
target("tests")
    set_kind("binary")
    set_default(false)
    
    add_deps("agent-core")
    add_packages("boost", "nlohmann_json", "spdlog", "libcurl", "yaml-cpp")
    add_includedirs("include", {public = true})
    add_includedirs("tests", {public = true})
    
    add_files("tests/main.cpp")
    
    if is_plat("linux") then
        add_syslinks("pthread", "dl")
    elseif is_plat("macosx") then
        add_syslinks("pthread", "dl")
    end
