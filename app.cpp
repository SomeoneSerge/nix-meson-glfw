#define GLEW_STATIC

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cassert>
#include <iostream>
#include <string>

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

GLFWwindow *makeWindow(const std::string &title, const int width = 800,
                       const int height = 600) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

  GLFWwindow *window =
      glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

  return window;
}

int main() {
  if (!glfwInit()) {
    std::cerr << "glfwInit() failed" << std::endl;
    return 1;
  }

  GLFWwindow *window = makeWindow("nix-meson-glfw");
  glfwMakeContextCurrent(window);

  glewExperimental = GL_TRUE;
  glewInit();

  if (glGenBuffers == nullptr) {
    std::cerr << "glewInit() failed" << std::endl;
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");
  ImGui::StyleColorsDark();

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

  if (vtxCompileStatus != GL_TRUE) {
    std::cerr << "Vertex shader compilation failed" << std::endl;
    return 1;
  }

  GLint fragCompileStatus;
  glGetShaderiv(fragShader, GL_COMPILE_STATUS, &fragCompileStatus);
  if (fragCompileStatus != GL_TRUE) {
    std::cerr << "Fragment shader compilation failed" << std::endl;
    return 1;
  }

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
    glfwPollEvents();

    glClearColor(.45f, .55f, .6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Demo window");
    ImGui::Button("Demo button");
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    int dispWidth, dispHeight;
    glfwGetFramebufferSize(window, &dispWidth, &dispHeight);
    glViewport(0, 0, dispWidth, dispHeight);
    glfwSwapBuffers(window);
  }

  /* why tf those aren't called upon something leaving the scope :facepalm: */
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  std::cout << "Hello, white triangle and imgui!" << std::endl;

  glDeleteProgram(shaderProgram);
  glDeleteShader(vtxShader);
  glDeleteShader(fragShader);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glfwTerminate();
  return 0;
}
