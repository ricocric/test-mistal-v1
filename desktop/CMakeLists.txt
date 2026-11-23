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
        foreach(_dir IN ITEMS "C:/libs" "$ENV{USERPROFILE}/libs" "$ENV{USERPROFILE}/Downloads" "$ENV{USERPROFILE}/Desktop")
            file(GLOB _cand "${_dir}/*glfw*" "${_dir}/*GLFW*")
            foreach(_c IN LISTS _cand)
                if(IS_DIRECTORY "${_c}" AND EXISTS "${_c}/include/GLFW/glfw3.h")
                    set(GLFW_HOME "${_c}")
                    break()
                endif()
            endforeach()
            if(GLFW_HOME)
                break()
            endif()
        endforeach()
    endif()
    if(NOT GLFW_HOME)
        message(FATAL_ERROR
            "GLFW introuvable :\n"
            "  - soit GLFW est installe systeme (vcpkg, MSYS2, homebrew) ;\n"
            "  - soit recompilez avec -DGLFW_HOME=<chemin vers le dossier GLFW dezippe>,\n"
            "    exemple : cmake -B build -DGLFW_HOME=C:/libs/glfw-3.5.1.bin.WIN64")
    endif()
    set(glfw3_INCLUDE_DIR "${GLFW_HOME}/include")
    find_library(glfw3_LIBRARY glfw3dll HINTS "${GLFW_HOME}/lib-vc2022")
    get_filename_component(S3D_GLFW_LIBDIR "${glfw3_LIBRARY}" DIRECTORY)
    set(S3D_GLFW_DLL "${S3D_GLFW_LIBDIR}/glfw3.dll")
    if(NOT EXISTS "${glfw3_INCLUDE_DIR}/GLFW/glfw3.h")
        message(FATAL_ERROR
            "GLFW_HOME = ${GLFW_HOME}\n"
            "ce dossier ne contient pas include/GLFW/glfw3.h\n"
            "Verifier que le chemin pointe vers le dossier extrait de l archive GLFW\n"
            "(celui qui contient directement include/ et lib-vc2022/), pas un parent\n"
            "ni un dossier vide. Test rapide sous PowerShell :\n"
            "  Get-ChildItem -Recurse -Filter glfw3.h C:/libs | Select-Object FullName")
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

if(S3D_GLFW_DLL AND EXISTS "${S3D_GLFW_DLL}")
    add_custom_command(TARGET sketch3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${S3D_GLFW_DLL}" "$<TARGET_FILE_DIR:sketch3d>")
endif()

if(MSVC)
    target_compile_options(sketch3d PRIVATE /W4)
else()
    target_compile_options(sketch3d PRIVATE -Wall -Wextra)
endif()
