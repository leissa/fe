add_executable(fe-test test.cpp)
target_compile_features(fe-test PRIVATE cxx_std_20)
target_link_libraries(fe-test PRIVATE doctest fe)
