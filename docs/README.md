# FE

![docs](https://img.shields.io/github/actions/workflow/status/leissa/fe/doxygen.yml?logo=gitbook&logoColor=white&label=docs&link=https%3A%2F%2Fleissa.github.io%2Ffe%2F&link=https%3A%2F%2Fgithub.com%2Fleissa%2Ffe%2Factions%2Fworkflows%2Fdoxygen.yml)
![linux](https://img.shields.io/github/actions/workflow/status/leissa/fe/linux.yml?logo=linux&logoColor=white&label=linux&link=https%3A%2F%2Fgithub.com%2Fleissa%2Ffe%2Factions%2Fworkflows%2Flinux.yml)
![macos](https://img.shields.io/github/actions/workflow/status/leissa/fe/macos.yml?logo=apple&logoColor=white&label=macos&link=https%3A%2F%2Fgithub.com%2Fleissa%2Ffe%2Factions%2Fworkflows%2Fmacos.yml)
![windows](https://img.shields.io/github/actions/workflow/status/leissa/fe/windows.yml?logo=windows&logoColor=white&label=windows&link=https%3A%2F%2Fgithub.com%2Fleissa%2Ffe%2Factions%2Fworkflows%2Fwindows.yml)

A header-only C++ library for writing compiler/interpreter frontends.

## Get Started Now!

Simply [fork](https://github.com/leissa/let/fork) the toy language *[Let](https://github.com/leissa/let)*.

## Building

FE has optional support [Abseil](https://abseil.io/)'s excelent [hash containers](https://abseil.io/docs/cpp/guides/container).
In order to enable Abseil support, you have to define `FE_ABSL`.
Otherwise, FE will fall back to the hash containers of the C++ standard library.

### Option #1: Include FE as Submodule

Add FE as external submodule to your compiler project:
```sh
git submodule add ../../leissa/fe external/fe
```
Add this to your `CMakeLists.txt`:
```cmake
set(FE_ABSL ON) # remove this line if you don't want to use Abseil
add_subdirectory(external/fe)

target_link_libraries(my_compiler_target PUBLIC fe)
```

### Option #2: Directly include FE in your Source Tree

1. Copy over the headers from FE to your compiler project:
    ```sh
    mkdir -p my_compiler_project/include/fe
    cp -r fe/include/fe/*.h my_compiler_project/include/fe
    ```
2. Put a `config.h` file into `include/fe`:
    1. With Abseil support:
    ```sh
    echo "pragma once\n\n#define FE_ABSL" >> my_compiler/include/fe/config.h
    ```
    2. Without Abseil support:
    ```sh
    touch my_compiler/include/fe/config.h
    ```
