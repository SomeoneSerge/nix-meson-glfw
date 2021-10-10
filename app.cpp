#define GLEW_STATIC

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cassert>
#include <iostream>
#include <string>

class NoCopy {
public:
  NoCopy() = default;
  virtual ~NoCopy() = default;
  NoCopy(const NoCopy &) = delete;
  NoCopy operator=(const NoCopy &) = delete;
};

class SafeGlfwCtx : NoCopy {
public:
  SafeGlfwCtx() {
    if (!glfwInit()) {
      throw std::runtime_error("glfwInit() failed");
    }
  };
  ~SafeGlfwCtx() { glfwTerminate(); }
};

class SafeGlfwWindow : NoCopy {
public:
  SafeGlfwWindow() {
    const auto width = 800;
    const auto height = 600;
    /* will make'em params later */
    const char title[] = "check out nix-meson-glfw";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    _window = glfwCreateWindow(width, height, title, nullptr, nullptr);
  }

  ~SafeGlfwWindow() { /* uh, apparently no need */
  }

  GLFWwindow *window() const { return _window; }
  void makeContextCurrent() const { glfwMakeContextCurrent(_window); }

private:
  GLFWwindow *_window;
};

class SafeGlew : NoCopy {
public:
  SafeGlew() {
    glewExperimental = GL_TRUE;
    glewInit();

    if (glGenBuffers == nullptr) {
      throw std::runtime_error("glewInit() failed");
    }
  }
};

class SafeImGui : NoCopy {
public:
  SafeImGui(GLFWwindow *window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    ImGui::StyleColorsDark();
  }
  ~SafeImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }
};

class SafeVBO : NoCopy {
public:
  SafeVBO(GLsizeiptr size, const void *data, GLenum target = GL_ARRAY_BUFFER,
          GLenum usage = GL_STATIC_DRAW) {
    /* this asks for a slot or a name or whatever it is opengl recognizes */
    glGenBuffers(1, &_vbo);

    /* this makes vbo the __active__ __array__ buffer... ie the target of the
     * next glBufferData command */
    glBindBuffer(target, _vbo);

    /* and this uploads vtxs into that slot */
    glBufferData(target, size, data, usage);
  }

  ~SafeVBO() { glDeleteBuffers(1, &_vbo); }

  GLuint vbo() const { return _vbo; }

private:
  GLuint _vbo;
};

class SafeVAO : NoCopy {
public:
  SafeVAO() {
    glGenVertexArrays(1, &_vao);
    bind();
  }

  ~SafeVAO() { glDeleteVertexArrays(1, &_vao); }

  GLuint vao() const { return _vao; }
  void bind() { glBindVertexArray(_vao); }

private:
  GLuint _vao;
};

class SafeShader : NoCopy {
public:
  SafeShader(GLenum shaderType, const char *source) {
    _shader = glCreateShader(shaderType);
    glShaderSource(_shader, 1, &source, nullptr);
    glCompileShader(_shader);

    GLint compileStatus;
    glGetShaderiv(_shader, GL_COMPILE_STATUS, &compileStatus);

    if (compileStatus != GL_TRUE) {
      throw std::runtime_error("Shader compilation failed");
    }
  }
  ~SafeShader() { glDeleteShader(_shader); }

  GLuint shader() const { return _shader; }

private:
  GLuint _shader;
};

class SafeShaderProgram : NoCopy {
public:
  SafeShaderProgram() {
    /* we could do the linking to shaders, etc, right here
     * but atm we only care about the order of initialization
     * (and destruction)
     */
    _program = glCreateProgram();
  }

  ~SafeShaderProgram() { glDeleteProgram(_program); }

  GLuint program() const { return _program; }

private:
  GLuint _program;
};

class VtxFragProgram : NoCopy {
public:
  VtxFragProgram(const char *vtxShader, const char *fragShader)
      : _vtxShader(GL_VERTEX_SHADER, vtxShader),
        _fragShader(GL_FRAGMENT_SHADER, fragShader) {
    glAttachShader(_program.program(), _vtxShader.shader());
    glAttachShader(_program.program(), _fragShader.shader());
  }

  GLuint vtxShader() const { return _vtxShader.shader(); }
  GLuint fragShader() const { return _fragShader.shader(); }
  GLuint program() const { return _program.program(); }

protected:
  SafeShader _vtxShader;
  SafeShader _fragShader;
  SafeShaderProgram _program;
};

class GlfwFrame : NoCopy {
public:
  GlfwFrame(GLFWwindow *window) : window(window) {
    glfwPollEvents();

    glClearColor(.45f, .55f, .6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
      glfwSetWindowShouldClose(window, GL_TRUE);
    }
  }
  ~GlfwFrame() {
    int dispWidth, dispHeight;
    glfwGetFramebufferSize(window, &dispWidth, &dispHeight);
    glViewport(0, 0, dispWidth, dispHeight);
    glfwSwapBuffers(window);
  }

private:
  GLFWwindow *window;
};

class ImGuiGlfwFrame : NoCopy {
public:
  ImGuiGlfwFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }
  ~ImGuiGlfwFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  }
};

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
  SafeGlfwCtx ctx;
  SafeGlfwWindow safeWindow;
  safeWindow.makeContextCurrent();

  GLFWwindow *window = safeWindow.window();

  SafeGlew glew;
  SafeImGui imguiContext(safeWindow.window());

  float vtxs[] = {0.0f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};
  SafeVBO safeVbo(sizeof(vtxs), vtxs);

  VtxFragProgram triangleProgram(vtxSource, fragSource);

  /* TODO: handle inputs/outputs of the shaders */
  glBindFragDataLocation(triangleProgram.program(), 0, "outColor");

  glLinkProgram(triangleProgram.program());
  glUseProgram(triangleProgram.program());

  SafeVAO triangleVao;

  GLint posAttrib = glGetAttribLocation(triangleProgram.program(), "position");
  glVertexAttribPointer(posAttrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(posAttrib);

  while (!glfwWindowShouldClose(window)) {
    GlfwFrame glfwFrame(window);
    ImGuiGlfwFrame imguiFrame;

    glDrawArrays(GL_TRIANGLES, 0, 3);

    ImGui::Begin("Demo window");
    ImGui::Button("Demo button");
    ImGui::End();
  }

  std::cout << "Hello, white triangle and imgui!" << std::endl;

  return 0;
}
