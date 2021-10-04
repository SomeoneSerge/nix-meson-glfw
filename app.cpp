#define GLEW_STATIC

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cassert>
#include <iostream>

const char *vtxSource = R"glsl(
    #version 150 core

    in vec2 position;

    void main()
    {
        gl_Position = vec4(position, 0.0, 1.0);
    }
)glsl";

const char *fragSource = R"glsl(
   #version 150 core
   
   out vec4 outColor;

   void main()
   {
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
   }
)glsl";

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

  float vtxs[] = {0.0f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};

  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vtxs), vtxs, GL_STATIC_DRAW);

  GLuint vtxShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vtxShader, 1, &vtxSource, nullptr);
  glCompileShader(vtxShader);

  GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragShader, 1, &fragSource, nullptr);
  glCompileShader(fragShader);

  GLint vtxCompileStatus;
  glGetShaderiv(vtxShader, GL_COMPILE_STATUS, &vtxCompileStatus);
  assert(("Vertex shader compilation", vtxCompileStatus == GL_TRUE));

  GLint fragCompileStatus;
  glGetShaderiv(fragShader, GL_COMPILE_STATUS, &fragCompileStatus);
  assert(("Fragment shader compilation", fragCompileStatus == GL_TRUE));

  GLuint shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vtxShader);
  glAttachShader(shaderProgram, fragShader);

  glBindFragDataLocation(shaderProgram, 0, "outColor");

  glLinkProgram(shaderProgram);
  glUseProgram(shaderProgram);

  GLuint vao;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  GLint posAttrib = glGetAttribLocation(shaderProgram, "position");
  glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(posAttrib);

  while (!glfwWindowShouldClose(window)) {
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glfwSwapBuffers(window);
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }
  }

  std::cout << "Hello, white triangle!" << std::endl;

  glDeleteProgram(shaderProgram);
  glDeleteShader(vtxShader);
  glDeleteShader(fragShader);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glfwTerminate();
  return 0;
}
