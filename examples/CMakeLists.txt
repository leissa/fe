add_executable(calc calc.cpp)
target_compile_features(calc PRIVATE cxx_std_20)
target_link_libraries(calc fe)
