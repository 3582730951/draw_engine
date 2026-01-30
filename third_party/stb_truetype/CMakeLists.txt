cmake_minimum_required(VERSION 3.16)

project(stb_truetype C)

add_library(stb_truetype STATIC stb_truetype.c)
target_include_directories(stb_truetype PUBLIC ${CMAKE_CURRENT_LIST_DIR})

install(TARGETS stb_truetype DESTINATION lib)
install(FILES stb_truetype.h DESTINATION include)
