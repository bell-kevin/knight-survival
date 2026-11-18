# ==============================================================================
# CMakeLists.txt - CMake Build Configuration File
# ==============================================================================
# INSTRUCTIONS:
# This file controls how your C++ project is built using CMake.
# Follow the instructions in the comments below to configure it
# for your project.
#
# HOW TO BUILD:
# 1. Create a build directory:    mkdir build
# 2. Navigate to build directory: cd build
# 3. Run CMake:                   cmake ..
# 4. Build the project:           make
# ==============================================================================

# ------------------------------------------------------------------------------
# Minimum CMake Version
# ------------------------------------------------------------------------------
# Specify the minimum version of CMake required to build this project.
# It is recommended to use at least version 3.10 for modern CMake features.
# INSTRUCTIONS: You can update this version if your project requires a newer
# version of CMake. Run "cmake --version" to check your installed version.
# ------------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.10)

# ------------------------------------------------------------------------------
# Project Name and Version
# ------------------------------------------------------------------------------
# Define the name of your project and its version number.
# INSTRUCTIONS: Replace "MyProject" with the name of your project.
#               Update the version number as needed (MAJOR.MINOR.PATCH).
#               Replace "CXX" with the languages your project uses:
#               - "C" for C projects
#               - "CXX" for C++ projects
#               - "C CXX" for projects that use both C and C++
# ------------------------------------------------------------------------------
project(MyProject VERSION 1.0.0 LANGUAGES CXX)

# ------------------------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------------------------
# Specify the C++ standard to use when compiling the project.
# INSTRUCTIONS: Update the value of CMAKE_CXX_STANDARD to the C++ standard
#               your project requires:
#               - 11 for C++11
#               - 14 for C++14
#               - 17 for C++17
#               - 20 for C++20
#               - 23 for C++23
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True) # Enforce the specified C++ standard
set(CMAKE_CXX_EXTENSIONS OFF)         # Disable compiler-specific extensions

# ------------------------------------------------------------------------------
# Build Type
# ------------------------------------------------------------------------------
# Specify the build type for the project.
# INSTRUCTIONS: You can set the build type here or pass it as a command line
#               argument when running CMake:
#               cmake .. -DCMAKE_BUILD_TYPE=Debug
#               cmake .. -DCMAKE_BUILD_TYPE=Release
#
#               Available build types:
#               - Debug:          No optimizations, includes debug symbols
#               - Release:        Full optimizations, no debug symbols
#               - RelWithDebInfo: Optimizations with debug symbols
#               - MinSizeRel:     Optimized for minimum binary size
# ------------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug) # Default to Debug if not specified
endif()

# ------------------------------------------------------------------------------
# Source Files
# ------------------------------------------------------------------------------
# Define the source files to be compiled.
# INSTRUCTIONS: Add all your source files to the list below.
#               You can use the GLOB pattern to automatically include all
#               source files in a directory, or list them explicitly.
#
#               Option 1 - Explicit list (recommended):
#               set(SOURCES
#                   src/main.cpp
#                   src/myclass.cpp
#                   src/utils.cpp
#               )
#
#               Option 2 - Automatic GLOB (not recommended for large projects
#               as CMake may not detect new files without re-running cmake):
#               file(GLOB_RECURSE SOURCES "src/*.cpp")
# ------------------------------------------------------------------------------
set(SOURCES
    src/main.cpp  # Add additional source files below this line
    # src/myclass.cpp
    # src/utils.cpp
)

# ------------------------------------------------------------------------------
# Executable Target
# ------------------------------------------------------------------------------
# Define the executable target to be built from the source files.
# INSTRUCTIONS: Replace "MyProject" with the name of your executable.
#               This is the name of the binary that will be generated
#               in the build directory after running make.
# ------------------------------------------------------------------------------
add_executable(MyProject ${SOURCES}
    src/net.h)
target_link_libraries(MyProject SDL2)

# ------------------------------------------------------------------------------
# Include Directories
# ------------------------------------------------------------------------------
# Specify the directories where CMake should look for header files.
# INSTRUCTIONS: Add any directories containing header files your project
#               uses. Common examples:
#               - "include" for a local include directory
#               - "/usr/local/include" for system-wide headers
#
#               Uncomment and update the line below to add include directories:
# ------------------------------------------------------------------------------
target_include_directories(MyProject PRIVATE
    ${CMAKE_SOURCE_DIR}/include  # Local include directory (e.g., include/*.h)
    # /path/to/other/includes    # Add additional include directories here
)

# ------------------------------------------------------------------------------
# Compiler Options
# ------------------------------------------------------------------------------
# Specify additional compiler options for the project.
# INSTRUCTIONS: Add any compiler flags your project requires.
#               Common examples:
#               - "-Wall"   Enable all warnings
#               - "-Wextra" Enable extra warnings
#               - "-O2"     Enable level 2 optimizations (for release builds)
#
#               Uncomment and update the line below to add compiler options:
# ------------------------------------------------------------------------------
target_compile_options(MyProject PRIVATE
    -Wall    # Enable all warnings
    -Wextra  # Enable extra warnings
    # -O2    # Uncomment for release build optimizations
)

# ------------------------------------------------------------------------------
# Linking Libraries
# ------------------------------------------------------------------------------
# Specify any libraries your project needs to link against.
# INSTRUCTIONS: Add any libraries your project depends on.
#               Common examples:
#               - "pthread" for POSIX threads
#               - "SDL2"    for SDL2 graphics library
#               - "m"       for the math library
#
#               Uncomment and update the lines below to link libraries:
# ------------------------------------------------------------------------------
# target_link_libraries(MyProject PRIVATE
#     pthread  # POSIX threads library
#     SDL2     # SDL2 graphics library
#     m        # Math library
# )

# ------------------------------------------------------------------------------
# Finding External Packages
# ------------------------------------------------------------------------------
# Use CMake's find_package() to locate and link external libraries.
# INSTRUCTIONS: Uncomment and update the lines below to find and link
#               external packages your project depends on.
#               Common examples:
#               - find_package(SDL2 REQUIRED)
#               - find_package(OpenGL REQUIRED)
#               - find_package(Boost REQUIRED COMPONENTS filesystem)
# ------------------------------------------------------------------------------
# find_package(SDL2 REQUIRED)
# target_link_libraries(MyProject PRIVATE SDL2::SDL2)

# find_package(OpenGL REQUIRED)
# target_link_libraries(MyProject PRIVATE OpenGL::GL)

# ------------------------------------------------------------------------------
# Output Directories
# ------------------------------------------------------------------------------
# Specify where the compiled binary and other build artifacts are placed.
# INSTRUCTIONS: Update the paths below to change the output directories
#               for the compiled binary and other build artifacts.
#               By default, the binary is placed in the build directory.
# ------------------------------------------------------------------------------
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # Output directory for the binary

# ------------------------------------------------------------------------------
# Installation Rules (Optional)
# ------------------------------------------------------------------------------
# Define installation rules for the project.
# INSTRUCTIONS: Uncomment and update the lines below to define installation
#               rules for your project. This allows you to install the binary
#               and other files to a specific location using "make install".
#               Common examples:
#               - DESTINATION bin   Install the binary to /usr/local/bin
#               - DESTINATION lib   Install libraries to /usr/local/lib
# ------------------------------------------------------------------------------
# install(TARGETS MyProject DESTINATION bin)
# install(FILES ${CMAKE_SOURCE_DIR}/include/myheader.h DESTINATION include)

# ------------------------------------------------------------------------------
# Testing (Optional)
# ------------------------------------------------------------------------------
# Enable testing for the project using CTest.
# INSTRUCTIONS: Uncomment the lines below to enable testing for your project.
#               Add your test executables and commands using add_test().
# ------------------------------------------------------------------------------
# enable_testing()
# add_test(NAME MyTest COMMAND MyProject --test)

# ==============================================================================
# END OF CMakeLists.txt
# ==============================================================================