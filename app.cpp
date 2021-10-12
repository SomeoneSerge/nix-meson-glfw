#define GLEW_STATIC

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include <OpenImageIO/imageio.h>

#include <clipp.h>

// FIXME:
constexpr double WINDOW_MIN_WIDTH = 800;

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

class NoCopy {
public:
  NoCopy() = default;
  virtual ~NoCopy() = default;
  NoCopy(NoCopy &&) = delete; /* let's delete the moves for now as well */
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

  ~SafeGlfwWindow() { glfwDestroyWindow(_window); }

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
    ImPlot::CreateContext();
    ImGui::StyleColorsDark();
  }
  ~SafeImGui() {
    ImPlot::DestroyContext();
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

struct Uint8Image {
  int xres;
  int yres;
  int channels;
  std::unique_ptr<unsigned char[]> data;

  Uint8Image(int xres, int yres, int channels,
             std::unique_ptr<unsigned char[]> &&data)
      : xres(xres), yres(yres), channels(channels), data(std::move(data)) {}

  Uint8Image(Uint8Image &&other)
      : xres(other.xres), yres(other.yres), data(std::move(other.data)) {}
  Uint8Image(const Uint8Image &other) = delete;
};

Uint8Image oiioLoadImage(const std::string &filename) {
  using namespace OIIO;

  auto in = ImageInput::open(filename);
  if (!in)
    throw std::runtime_error("Couldn't load the image");

  const ImageSpec &spec = in->spec();
  int xres = spec.width;
  int yres = spec.height;
  int channels = spec.nchannels;
  std::unique_ptr<unsigned char[]> data =
      std::make_unique<unsigned char[]>(xres * yres * channels);

  if (!data)
    throw std::runtime_error("Couldn't allocate memory for image data");

  in->read_image(TypeDesc::UINT8, data.get());
  in->close(); /* eh... why not raii, I now have to wrap it in try-catch and I
                  don't want to */

  return Uint8Image(xres, yres, channels, std::move(data));
}

class SafeGlTexture : NoCopy {
public:
  SafeGlTexture(const Uint8Image &image)
      : _texture(0), _xres(image.xres), _yres(image.yres) {
    glGenTextures(1, &_texture);
    glBindTexture(GL_TEXTURE_2D, _texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const auto mode = image.channels == 3 ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, mode, _xres, _yres, 0, mode,
                 GL_UNSIGNED_BYTE, image.data.get());
  }

  ~SafeGlTexture() { glDeleteTextures(1, &_texture); }

  GLuint texture() const { return _texture; }
  void *textureVoidStar() const { return (void *)(intptr_t)_texture; }
  void bind() { glBindTexture(GL_TEXTURE_2D, _texture); }
  int xres() const { return _xres; }
  int yres() const { return _yres; }
  double aspect() const { return _yres * 1.0 / _xres; }

private:
  GLuint _texture;
  int _xres, _yres;
};

struct AppArgs {
  std::string image0Path;
  std::string image1Path;

  static AppArgs parse(int argc, char *argv[]) {
    using namespace clipp;

    std::string image0Path, image1Path;
    auto cli = (value("Path to image A", image0Path),
                value("Path to image B", image1Path));

    if (!clipp::parse(argc, argv, cli)) {
      std::cerr << make_man_page(cli, argv[0]);
      std::exit(1);
    }
    return AppArgs{image0Path, image1Path};
  }
};

int main(int argc, char *argv[]) {

  AppArgs args = AppArgs::parse(argc, argv);

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

  // FIXME:
  SafeGlTexture image0(oiioLoadImage(args.image0Path));
  SafeGlTexture image1(oiioLoadImage(args.image1Path));

  while (!glfwWindowShouldClose(window)) {
    GlfwFrame glfwFrame(window);
    ImGuiGlfwFrame imguiFrame;

    glDrawArrays(GL_TRIANGLES, 0, 3);

    struct {
      int x, y;
    } glfwSize;
    glfwGetWindowSize(window, &glfwSize.x, &glfwSize.y);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(glfwSize.x, .5 * glfwSize.x * image0.aspect()),
        ImVec2(glfwSize.x,
               std::max(glfwSize.y * 1.0, .5 * glfwSize.x * image0.aspect())));
    ImGui::Begin("Window0", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoResize);

    const auto frameSize = ImGui::GetWindowSize();
    const auto plotSize =
        ImVec2(frameSize.x, .5 * frameSize.x * image0.aspect());

    if (ImPlot::BeginPlot("Correspondences", nullptr, nullptr, plotSize,
                          ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased |
                              ImPlotFlags_Crosshairs)) {
      ImPlot::PlotImage("im0", image0.textureVoidStar(), ImPlotPoint(-1.0, 0.0),
                        ImPlotPoint(0.0, 1.0));
      ImPlot::PlotImage("im1", image1.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));
      ImPlot::EndPlot();
    }
    ImGui::End();
  }

  std::cout << "Hello, ImPlot!" << std::endl;

  return 0;
}
