cmake_minimum_required(VERSION 3.14)

project(TEST)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_WARNINGS_FLAGS "-Wall -Wpedantic -Wextra -Werror")
set(CMAKE_CXX_FLAGS "-g -O2 -march=x86-64 -std=c++20 ${CMAKE_WARNINGS_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/../ -I${CMAKE_CURRENT_SOURCE_DIR}/../fdaPDE/core/")

find_package(Eigen3 3.4 REQUIRED NO_MODULE)

include_directories(../src/)
include_directories(../../fdaPDE/)

# Trova e abilita OpenMP
find_package(OpenMP REQUIRED)

# Abilita la funzionalità di testing usando gtest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/f8d7d77c06936315286eb55f8de22cd23c188571.zip
)

# Per Windows: prevenire la sovrascrittura delle impostazioni del compilatore/linker del progetto padre
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Aggiungi l'eseguibile
add_executable(fdapde_test main.cpp)

# Linka Eigen, gtest e OpenMP in un'unica chiamata
target_link_libraries(fdapde_test PRIVATE Eigen3::Eigen gtest_main)

# Linka OpenMP
if(OpenMP_CXX_FOUND)
    target_compile_options(fdapde_test PRIVATE ${OpenMP_CXX_FLAGS})
    target_link_libraries(fdapde_test PRIVATE ${OpenMP_CXX_LIBRARIES})
endif()

add_test(NAME fdapde_test
	 COMMAND $<TARGET_FILE:fdapde_test>
)

include(GoogleTest)
gtest_discover_tests(fdapde_test)