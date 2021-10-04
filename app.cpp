#define GLEW_STATIC

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cassert>
#include <iostream>

int main() {
  glfwInit();

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

  GLFWwindow *window =
      glfwCreateWindow(800, 600, "nix-meson-glfw", nullptr, nullptr);

  glfwMakeContextCurrent(window);

  glewExperimental = GL_TRUE;
  glewInit();

  assert(("GLEW initialization", glGenBuffers != nullptr));
  GLuint vtxBuffer;
  glGenBuffers(1, &vtxBuffer);

  while (!glfwWindowShouldClose(window)) {
    glfwSwapBuffers(window);
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }
  }

  std::cout << "Hello, meson!" << std::endl
            << "Test vtxBuffer: " << vtxBuffer << std::endl;

  glfwTerminate();
  return 0;
}
