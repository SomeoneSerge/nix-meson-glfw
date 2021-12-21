#include "viscor/raii.h"

using namespace VisCor;

VisCor::SafeGlfwWindow::SafeGlfwWindow() {
  const auto width = WINDOW_MIN_WIDTH;
  const auto height = WINDOW_MIN_WIDTH * (9.0 / 16.0) * .5;
  glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);
  /* will make'em params later */
  const char title[] = "check out nix-meson-glfw";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  _window = glfwCreateWindow(width, height, title, nullptr, nullptr);
}
VisCor::SafeGlfwWindow::~SafeGlfwWindow() { glfwDestroyWindow(_window); }
GLFWwindow *VisCor::SafeGlfwWindow::window() const { return _window; }
void VisCor::SafeGlfwWindow::makeContextCurrent() const {
  glfwMakeContextCurrent(_window);
}

VisCor::SafeGlfwCtx::SafeGlfwCtx() {
  if (!glfwInit()) {
    throw std::runtime_error("glfwInit() failed");
  }
}
VisCor::SafeGlfwCtx::~SafeGlfwCtx() { glfwTerminate(); }
VisCor::SafeGlew::SafeGlew() {
  glewExperimental = GL_TRUE;
  glewInit();

  if (glGenBuffers == nullptr) {
    throw std::runtime_error("glewInit() failed: glGenBuffers == nullptr");
  }
}

VisCor::SafeImGui::SafeImGui(GLFWwindow *window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 150");
  ImPlot::CreateContext();
  ImGui::StyleColorsDark();
}
VisCor::SafeImGui::~SafeImGui() {
  ImPlot::DestroyContext();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

VisCor::SafeVBO::SafeVBO(GLsizeiptr size, const void *data, GLenum target,
                         GLenum usage) {
  /* this asks for a slot or a name or whatever it is opengl recognizes */
  glGenBuffers(1, &_vbo);

  /* this makes vbo the __active__ __array__ buffer... ie the target of the
   * next glBufferData command */
  glBindBuffer(target, _vbo);

  /* and this uploads vtxs into that slot */
  glBufferData(target, size, data, usage);
}
VisCor::SafeVBO::~SafeVBO() { glDeleteBuffers(1, &_vbo); }
GLuint VisCor::SafeVBO::vbo() const { return _vbo; }

VisCor::SafeShader::SafeShader(GLenum shaderType, const char *source) {
  _shader = glCreateShader(shaderType);
  glShaderSource(_shader, 1, &source, nullptr);
  glCompileShader(_shader);

  GLint compileStatus;
  glGetShaderiv(_shader, GL_COMPILE_STATUS, &compileStatus);

  if (compileStatus != GL_TRUE) {
    throw std::runtime_error("Shader compilation failed");
  }
}
VisCor::SafeShader::~SafeShader() { glDeleteShader(_shader); }
GLuint VisCor::SafeShader::shader() const { return _shader; }

VisCor::VtxFragProgram::VtxFragProgram(const char *vtxShader,
                                       const char *fragShader)
    : _vtxShader(GL_VERTEX_SHADER, vtxShader),
      _fragShader(GL_FRAGMENT_SHADER, fragShader) {
  glAttachShader(_program.program(), _vtxShader.shader());
  glAttachShader(_program.program(), _fragShader.shader());
}
GLuint VisCor::VtxFragProgram::vtxShader() const { return _vtxShader.shader(); }
GLuint VisCor::VtxFragProgram::fragShader() const {
  return _fragShader.shader();
}
GLuint VisCor::VtxFragProgram::program() const { return _program.program(); }

VisCor::GlfwFrame::GlfwFrame(GLFWwindow *window) : window(window) {
  glfwPollEvents();

  glClearColor(.45f, .55f, .6f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GL_TRUE);
  }
}
VisCor::GlfwFrame::~GlfwFrame() {
  int dispWidth, dispHeight;
  glfwGetFramebufferSize(window, &dispWidth, &dispHeight);
  glViewport(0, 0, dispWidth, dispHeight);
  glfwSwapBuffers(window);
}

VisCor::ImGuiGlfwFrame::ImGuiGlfwFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}
VisCor::ImGuiGlfwFrame::~ImGuiGlfwFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

VisCor::SafeGlTexture::SafeGlTexture(const Uint8Image &image,
                                     const unsigned int interpolation)
    : _texture(0), _xres(image.xres), _yres(image.yres) {
  glGenTextures(1, &_texture);
  glBindTexture(GL_TEXTURE_2D, _texture);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, interpolation);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, interpolation);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  constexpr unsigned int modes[] = {0, GL_DEPTH_COMPONENT, 0, GL_RGB, GL_RGBA};
  const auto mode = modes[image.channels];
  glTexImage2D(GL_TEXTURE_2D, 0, mode, _xres, _yres, 0, mode, GL_UNSIGNED_BYTE,
               image.data.get());
}
VisCor::SafeGlTexture::~SafeGlTexture() {
  if (_texture != GL_INVALID_VALUE) {
    glDeleteTextures(1, &_texture);
  }
}
