add_executable(calc calc.cpp)
set_property(TARGET calc PROPERTY CXX_STANDARD 20)
target_link_libraries(calc fe)
