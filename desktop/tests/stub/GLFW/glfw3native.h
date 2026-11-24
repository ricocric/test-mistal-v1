/* Stub GLFW native pour le harnais de tests (aucune librairie reelle requise).
   Seul glfwGetWin32Window est reference par src/main.cpp (menus natifs). */
#pragma once
#include <windows.h>

HWND glfwGetWin32Window(GLFWwindow* window);
