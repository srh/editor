cmake_minimum_required(VERSION 3.22.1)

project(
  Qwertillion
  LANGUAGES CXX)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(qwi
  buffer.cpp chars.cpp io.cpp main.cpp movement.cpp state.cpp terminal.cpp
  term_ui.cpp undo.cpp util.cpp)
set_property(TARGET qwi PROPERTY CXX_STANDARD 20)

target_link_libraries(qwi PRIVATE Threads::Threads)
