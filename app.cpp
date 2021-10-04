#include <GLFW/glfw3.h>
#include <iostream>

int main() {
  glfwInit();
  std::cout << "Hello, meson!" << std::endl;
  glfwTerminate();
  return 0;
}
