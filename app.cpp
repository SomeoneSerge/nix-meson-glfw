#define GLEW_STATIC

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <string>

#include <OpenImageIO/imageio.h>

#include <clipp.h>
#include <filesystem>
#include <nlohmann/json.hpp>

#include <Eigen/Dense>

// FIXME:
constexpr double WINDOW_MIN_WIDTH = 800;

using namespace Eigen;
namespace fs = std::filesystem;

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
  std::string tracePath;
  bool fix01Scale = false;
  float heatmapAlpha = 0.6;

  AppArgs(int argc, char *argv[]) {
    using namespace clipp;

    bool image0set = false, image1set = false;

    auto cli =
        (value("Path to the heatmap dir (tifs plus layout.json)", tracePath),
         option("--image0").set(image0set) & opt_value("path", image0Path),
         option("--image1").set(image1set) & opt_value("path", image1Path),
         option("-01", "--fix-01-scale")
             .set(fix01Scale)
             .doc("Fix the heatmap scale to [0..1]. Otherwise, adjust to "
                  "current min/max values"),
         option("-a", "--heatmap-alpha") &
             value("alpha", heatmapAlpha) %
                 "Transparency of the heatmap overlay");

    if (!clipp::parse(argc, argv, cli)) {
      std::cerr << make_man_page(cli, argv[0]);
      std::exit(1);
    }
    if (!image0set) {
      image0Path = fs::path(tracePath) / "image0.tif";
    }
    if (!image1set) {
      image1Path = fs::path(tracePath) / "image1.tif";
    }
  }
};

struct LayoutJson {
  LayoutJson(const fs::path &path) {
    using json = nlohmann::json;
    std::ifstream fLayout(path);
    json j;
    fLayout >> j;

    shape = (std::vector<int>)j.at("shape");
    dtype = j.at("dtype");
  }
  std::vector<int> shape;
  std::string dtype;
};

std::tuple<int, int> indexFromName(const std::string &filename) {
  static const std::regex regex("(?:0*([0-9]+),)*0*([0-9]+)\\.tif");

  std::vector<int> indices;
  std::smatch sm;
  std::regex_match(filename, sm, regex);

  for (auto i = 1; i < sm.size(); ++i) {
    indices.push_back(std::stoi(sm[i]));
  }

  if (indices.size() != 2) {
    std::cerr << "Expected two indices in the name, got " << indices.size()
              << std::endl;
    throw std::runtime_error("Not a 4D field");
  }
  return std::make_tuple(indices[0], indices[1]);
}

struct HeatmapsDir {
  HeatmapsDir(const fs::path &layoutPath) : root(layoutPath.parent_path()) {
    const auto layout = LayoutJson(layoutPath);

    if (layout.shape.size() != 4)
      throw std::runtime_error("Not a 4D field");
    if (layout.dtype != "float32")
      throw std::runtime_error("only float32 supported");

    h0 = layout.shape[0];
    w0 = layout.shape[1];
    h1 = layout.shape[2];
    w1 = layout.shape[3];

    slices.reserve(h0 * w0);
    sliceIndices = -Eigen::Array<int, Dynamic, Dynamic, RowMajor>::Ones(h0, w0);

    for (auto &p : fs::directory_iterator(root)) {
      const auto name = p.path().filename();

      if (name.extension() != ".tif")
        continue;

      auto [i, j] = indexFromName(name);
      sliceIndices(i, j) = slices.size();
      slices.push_back(p.path());
    }
  }

  std::optional<fs::path> slicePath(int i, int j) const {
    return slices[sliceIndices(i, j)];
  }

  Matrix<float, Dynamic, Dynamic, RowMajor> slice(int i, int j) const {
    // TODO: cache

    using namespace OIIO;

    const auto path = slicePath(i, j);

    if (!path)
      return Eigen::Matrix<float, Dynamic, Dynamic, RowMajor>::Constant(
          h1, w1, std::numeric_limits<float>::quiet_NaN());

    auto in = ImageInput::open(path.value());
    if (!in) {
      std::cerr << "Couldn't load" << path.value() << std::endl;
      throw std::runtime_error("Couldn't load the image");
    }

    const ImageSpec &spec = in->spec();
    int xres = spec.width;
    int yres = spec.height;

    if (xres * yres == 0)
      throw std::runtime_error("Loaded image was empty");

    Matrix<float, Dynamic, Dynamic, RowMajor> m(yres, xres);

    in->read_image(TypeDesc::FLOAT, m.data());
    in->close(); /* eh... why not raii, I now have to wrap it in try-catch and I
                    don't want to */
    return m;
  }

  int h0, w0, h1, w1;
  fs::path root;
  std::vector<fs::path> slices;
  Eigen::Array<int, Dynamic, Dynamic, RowMajor> sliceIndices;
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

double normalizeAlpha(double alpha) {
  return std::max(std::min(std::round(alpha * 100.0) / 100.0, 1.0), 0.0);
}

std::string alphaToString(double alpha) {
  return std::to_string(normalizeAlpha(alpha));
}

ImPlotColormap colormapTransparentCopy(ImPlotColormap src, double alpha) {
  const int size = ImPlot::GetColormapSize(src);
  alpha = normalizeAlpha(alpha);
  const std::string name(alphaToString(alpha) + "_" +
                         std::string(ImPlot::GetColormapName(src)) + "_" +
                         std::to_string(size));

  {
    const auto id = ImPlot::GetColormapIndex(name.c_str());
    if (id != -1)
      return id;
  }

  std::vector<ImVec4> colors(size);
  for (int i = 0; i < size; ++i) {
    colors[i] = ImPlot::GetColormapColor(i, src);
    colors[i].w *= alpha;
  }

  const auto cmap =
      ImPlot::AddColormap(name.c_str(), colors.data(), size, false);
  return cmap;
}

struct Message {
  double u0 = .5, v0 = .5; // cursor position in plot 0
  double heatMin = 0, heatMax = 1;
  int iSlice = 0, jSlice = 0;
  float alpha = 1.0;
  int idVolume = 0;
  std::chrono::time_point<std::chrono::system_clock> lastHeatmapSwitch;
};

struct State : public Message {
  Matrix<float, Dynamic, Dynamic, RowMajor> heat;
};

struct TraceDir {
  TraceDir(fs::path root) {
    std::vector<fs::path> heatmaps;
    for (auto &p : fs::recursive_directory_iterator(root)) {
      if (p.path().filename() != "layout.json")
        continue;

      LayoutJson layout(p.path());
      if (layout.shape.size() != 4)
        continue;

      heatmaps.push_back(p);
    }

    names.reserve(heatmaps.size());
    namesCStr.reserve(heatmaps.size());
    volumes.reserve(heatmaps.size());

    for (auto &p : heatmaps)
      volumes.push_back(HeatmapsDir(p));

    for (auto &p : heatmaps)
      names.push_back(p.parent_path().lexically_relative(root).string());

    for (auto &s : names)
      namesCStr.push_back(s.c_str());
  }

  std::vector<HeatmapsDir> volumes;
  std::vector<std::string> names;
  std::vector<const char *> namesCStr;
};

int main(int argc, char *argv[]) {

  AppArgs args(argc, argv);

  TraceDir trace(args.tracePath);

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

  State state;
  state.alpha = args.heatmapAlpha;

  constexpr auto defaultWindowOptions = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoResize;
  constexpr auto defaultPlotOptions =
      ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased | ImPlotFlags_Crosshairs;

  while (!glfwWindowShouldClose(window)) {
    Message msg{.u0 = state.u0,
                .v0 = state.v0,
                .iSlice = state.iSlice,
                .jSlice = state.jSlice,
                .alpha = state.alpha,
                .idVolume = state.idVolume,
                .lastHeatmapSwitch = state.lastHeatmapSwitch};

    GlfwFrame glfwFrame(window);
    ImGuiGlfwFrame imguiFrame;

    glDrawArrays(GL_TRIANGLES, 0, 3);

    struct {
      int x, y;
    } glfwSize;
    glfwGetWindowSize(window, &glfwSize.x, &glfwSize.y);

    const auto toolboxHeight = ImGui::GetTextLineHeightWithSpacing() * 6;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(glfwSize.x, toolboxHeight),
                                        ImVec2(glfwSize.x, toolboxHeight));

    if (ImGui::Begin("Toolbox", nullptr, defaultWindowOptions)) {
      ImGui::SliderFloat("Heatmap Alpha", &msg.alpha, 0.0, 1.0);
      msg.alpha = normalizeAlpha(msg.alpha);

      ImGui::ListBox("Heatmaps", &msg.idVolume, trace.namesCStr.data(),
                     trace.namesCStr.size());
    }
    ImGui::End();

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      const auto t = std::chrono::system_clock::now();
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          t - state.lastHeatmapSwitch);
      if (ms.count() > 300) {
        msg.idVolume = (msg.idVolume + 1) % trace.volumes.size();
        msg.lastHeatmapSwitch = t;
      }
    }

    auto &heatmaps = trace.volumes[msg.idVolume];
    const auto cmap =
        colormapTransparentCopy(ImPlotColormap_Viridis, msg.alpha);

    const auto workArea = ImVec2(glfwSize.x, glfwSize.y - toolboxHeight);
    const auto neededArea =
        ImVec2(workArea.x, .5 * workArea.x * image0.aspect());
    ImGui::SetNextWindowPos(ImVec2(0, toolboxHeight));
    ImGui::SetNextWindowSizeConstraints(
        neededArea,
        ImVec2(neededArea.x, std::max<double>(glfwSize.y, neededArea.y)));
    ImGui::Begin("Window0", nullptr, defaultWindowOptions);

    const auto frameSize = ImGui::GetWindowSize();
    const auto cmapWidth = 100;
    const auto plotSize =
        ImVec2(.5 * (frameSize.x - cmapWidth),
               .5 * (frameSize.x - cmapWidth) * image0.aspect());

    if (ImPlot::BeginPlot("Image0", nullptr, nullptr, plotSize,
                          defaultPlotOptions)) {
      ImPlot::PlotImage("im0", image0.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));

      const auto xyNew = ImPlot::GetPlotMousePos();
      ImPlotPoint xyDrag(state.u0, 1.0 - state.v0);

      const auto queryColor = ImVec4(255 / 255.0, 99 / 255.0, 71 / 255.0, 1.0);
      if (ImPlot::DragPoint("Query", &xyDrag.x, &xyDrag.y, true, queryColor, 6)) {
        xyDrag.x = xyNew.x;
        xyDrag.y = xyNew.y;
      }
      if (ImPlot::DragLineX("QueryX", &xyDrag.x, true, queryColor)) {
        xyDrag.x = xyNew.x;
      }
      if (ImPlot::DragLineY("QueryY", &xyDrag.y, true, queryColor)) {
        xyDrag.y = xyNew.y;
      }

      const auto uv = ImVec2(xyDrag.x, 1.0 - xyDrag.y);
      msg.u0 = uv.x;
      msg.v0 = uv.y;

      const auto i =
          std::max(0, std::min((int)(uv.y * heatmaps.h0), heatmaps.h0 - 1));
      const auto j =
          std::max(0, std::min((int)(uv.x * heatmaps.w0), heatmaps.w0 - 1));
      msg.iSlice = i;
      msg.jSlice = j;

      ImPlot::EndPlot();
    }

    ImGui::SameLine();

    if (ImPlot::BeginPlot("Image1", nullptr, nullptr, plotSize,
                          ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased |
                              ImPlotFlags_Crosshairs)) {
      bool cachedSlice = msg.idVolume == state.idVolume &&
                         msg.iSlice == state.iSlice &&
                         msg.jSlice == state.jSlice && state.heat.rows() > 0 &&
                         state.heat.cols() > 0;

      if (!cachedSlice) {
        state.heat = heatmaps.slice(msg.iSlice, msg.jSlice);
      }

      const auto heatmap = state.heat;

      if (args.fix01Scale) {
        msg.heatMin = 0;
        msg.heatMax = 1;
      } else {
        msg.heatMin = heatmap.minCoeff();
        msg.heatMax = heatmap.maxCoeff();
      }

      ImPlot::PlotImage("im1", image1.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));

      ImPlot::PushColormap(cmap);
      ImPlot::PlotHeatmap("Correspondence volume slice", heatmap.data(),
                          heatmap.rows(), heatmap.cols(), msg.heatMin,
                          msg.heatMax, nullptr);

      ImPlot::PopColormap();

      ImPlot::EndPlot();
    }

    ImPlot::PushColormap(ImPlotColormap_Viridis);
    ImGui::SameLine();
    ImPlot::ColormapScale("ColormapScale", msg.heatMin, msg.heatMax,
                          ImVec2(cmapWidth, plotSize.y));
    ImPlot::PopColormap();

    ImGui::End();

    static_cast<Message &>(state) = msg;
  }

  return 0;
}
