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

struct DummyHeatmap {
public:
  DummyHeatmap(int width, int height) : _w(width), _h(height), _data(_w * _h) {}

  double &operator()(int i, int j) { return _data[i * _w + j]; }
  const double &operator()(int i, int j) const { return _data[i * _w + j]; }

  double *data() { return _data.data(); }
  const double *data() const { return _data.data(); }

  double min() const { return _lo; }
  double max() const { return _hi; }

  int xres() const { return _w; }
  int yres() const { return _h; }

  template <typename F>
  void setSdf(const double u0, const double v0, const F &f) {
    _lo = std::numeric_limits<double>::max();
    _hi = std::numeric_limits<double>::min();
    for (int i = 0; i < _h; ++i) {
      for (int j = 0; j < _w; ++j) {
        const double u = (j + .5) / _w, v = (i + .5) / _h;
        const double sdf =
            std::sqrt(std::pow(u - u0, 2.0) + std::pow(v - v0, 2.0));
        // std::abs(u - u0) + std::abs(v - v0);
        const double val = f(sdf);
        (*this)(i, j) = val;
        _lo = std::min(val, _lo);
        _hi = std::max(val, _hi);
      }
    }
  }

private:
  int _w, _h;
  double _lo, _hi;
  std::vector<double> _data; // row-major 2D array
};

ImPlotColormap colormapTransparentResample(ImPlotColormap src, int newRes,
                                           double alpha) {
  const std::string name(std::string(ImPlot::GetColormapName(src)) + "-" +
                         std::to_string(newRes) + "-" + std::to_string(alpha));
  const int size = ImPlot::GetColormapSize(src);
  std::vector<ImVec4> colors(newRes);
  for (int i = 0; i < newRes; ++i) {
    const auto t = i / (newRes - 1.0);
    const auto j = t * (size - 1.0);
    const int j0 = std::floor(j);
    const int j1 = std::ceil(j);
    const auto c0 = ImPlot::GetColormapColor((int)j0, src);
    const auto c1 = ImPlot::GetColormapColor((int)j1, src);
    if (j1 == j0) {
      colors[i] = c0;
    } else {
      const auto u = j - j0;
      const auto u1 = j1 - j;
      colors[i] = ImVec4(u1 * c0.x + u * c1.x, u1 * c0.y + u * c1.y,
                         u1 * c0.z + u * c1.z, u1 * c0.w + u * c1.w);
    }
    colors[i] = c0;
    colors[i].w *= alpha;
  }
  return ImPlot::AddColormap(name.c_str(), colors.data(), newRes, false);
}

ImPlotColormap colormapTransparentCopy(ImPlotColormap src, double alpha) {
  const int size = ImPlot::GetColormapSize(src);
  const std::string name(std::string(ImPlot::GetColormapName(src)) + "-" +
                         std::to_string(size) + "-" + std::to_string(alpha));
  std::vector<ImVec4> colors(size);
  for (int i = 0; i < size; ++i) {
    colors[i] = ImPlot::GetColormapColor(i, src);
    colors[i].w *= alpha;
  }
  return ImPlot::AddColormap(name.c_str(), colors.data(), size, false);
}

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

  DummyHeatmap heatmap(image1.xres(), image1.yres());
  const auto cmap = colormapTransparentCopy(ImPlotColormap_Jet, .8);

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
    const auto cmapWidth = 64;
    const auto plotSize =
        ImVec2(frameSize.x - cmapWidth,
               .5 * (frameSize.x - cmapWidth) * image0.aspect());

    if (ImPlot::BeginPlot("Correspondences", nullptr, nullptr, plotSize,
                          ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased |
                              ImPlotFlags_Crosshairs)) {
      const auto xy = ImPlot::GetPlotMousePos();
      const auto uv = ImVec2(xy.x + 1.0, 1.0 - xy.y);
      heatmap.setSdf(uv.x, uv.y, [](const auto sdf) {
        return std::exp(-4 * std::pow(sdf, 2.0));
      });

      ImPlot::PlotImage("im0", image0.textureVoidStar(), ImPlotPoint(-1.0, 0.0),
                        ImPlotPoint(0.0, 1.0));
      ImPlot::PlotImage("im1", image1.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));

      ImPlot::PushColormap(cmap);
      ImPlot::PlotHeatmap("Correspondence volume slice", heatmap.data(),
                          heatmap.yres(), heatmap.xres(), heatmap.min(),
                          heatmap.max(), nullptr);

      ImPlot::PopColormap();

      ImPlot::EndPlot();
    }
    ImPlot::PushColormap(cmap);
    ImGui::SameLine();
    ImPlot::ColormapScale("ColormapScale", heatmap.min(), heatmap.max(),
                          ImVec2(cmapWidth, plotSize.y));
    ImPlot::PopColormap();
    ImGui::End();
  }

  std::cout << "Hello, heatmap!" << std::endl;

  return 0;
}
