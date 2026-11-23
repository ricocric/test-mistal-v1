cmake_minimum_required(VERSION 3.15)
project(Sketch3D CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG QUIET)
if(glfw3_FOUND)
    set(S3D_GLFW_TARGET glfw)
    message(STATUS "GLFW trouve via find_package")
else()
    set(GLFW_HOME "" CACHE PATH "Dossier racine des binaires GLFW (contient include/ et lib-vc2022/)")
    if(NOT GLFW_HOME)
        message(FATAL_ERROR
            "GLFW introuvable :\n"
            "  - soit GLFW est installe systeme (vcpkg, MSYS2, homebrew) ;\n"
            "  - soit recompilez avec -DGLFW_HOME=<chemin vers le dossier GLFW dezippe>,\n"
            "    exemple : cmake -B build -DGLFW_HOME=C:/libs/glfw-3.5.1.bin.WIN64")
    endif()
    set(glfw3_INCLUDE_DIR "${GLFW_HOME}/include")
    find_library(glfw3_LIBRARY glfw3dll HINTS "${GLFW_HOME}/lib-vc2022")
    if(NOT EXISTS "${glfw3_INCLUDE_DIR}/GLFW/glfw3.h")
        message(FATAL_ERROR "GLFW_HOME ne contient pas include/GLFW/glfw3.h")
    endif()
    if(NOT glfw3_LIBRARY)
        message(FATAL_ERROR "GLFW_HOME/lib-vc2022 ne contient pas glfw3dll.lib")
    endif()
    add_library(glfw SHARED IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${glfw3_LIBRARY}"
        IMPORTED_IMPLIB "${glfw3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${glfw3_INCLUDE_DIR}")
    set(S3D_GLFW_TARGET glfw)
    message(STATUS "GLFW trouve via GLFW_HOME = ${GLFW_HOME}")
endif()

find_package(OpenGL REQUIRED)

add_executable(sketch3d src/main.cpp)
target_link_libraries(sketch3d PRIVATE ${S3D_GLFW_TARGET} OpenGL::GL)

if(MSVC)
    target_compile_options(sketch3d PRIVATE /W4)
else()
    target_compile_options(sketch3d PRIVATE -Wall -Wextra)
endif()
