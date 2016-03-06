cmake_minimum_required(VERSION 2.8)
project(gossip)

find_package(Boost)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()
set(MY_CXX_FLAGS "-g -fprofile-arcs -ftest-coverage -std=c++0x")
set(MY_LINK_FLAGS "-lgcov")

set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${MY_CXX_FLAGS}" )
set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_CXX_FLAGS} ${MY_LINK_FLAGS}" )

add_library(member member.cpp)
add_executable(gossip main.cpp)
target_link_libraries(gossip member)
