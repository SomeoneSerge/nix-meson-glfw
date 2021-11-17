#define GLEW_STATIC

#include <msgpack.hpp> // Must go before OIIO

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <OpenImageIO/imageio.h>

#include <clipp.h>
#include <filesystem>

#include <Eigen/Dense>

#include "viscor/raii.h"
#include "viscor/utils.h"

using namespace Eigen;
using namespace VisCor;

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

struct AppArgs {
  std::string image0Path;
  std::string image1Path;
  std::string feat0Path;
  std::string feat1Path;
  bool fix01Scale = false;
  float heatmapAlpha = 0.6;

  AppArgs(int argc, char *argv[]) {
    using namespace clipp;

    bool image0set = false, image1set = false;

    auto cli =
        (value("Path to the first featuremap", feat0Path),
         value("Path to the second featuremap", feat1Path),
         option("--image0").set(image0set) & value("path", image0Path),
         option("--image1").set(image1set) & value("path", image1Path),
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
      // image0Path = fs::path(tracePath) / "image0.tif";
    }
    if (!image1set) {
      // image1Path = fs::path(tracePath) / "image1.tif";
    }
  }
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

struct DescriptorField {
  std::tuple<int, int, int> shape;
  Eigen::Array<float, Dynamic, Dynamic, Eigen::RowMajor> data;

  DescriptorField(const DescriptorField &) = delete;
  DescriptorField() = default;
  DescriptorField(DescriptorField &&) = default;

  Eigen::Array<float, 1, Dynamic> operator()(const int i, const int j) const {
    return data.row(i * std::get<1>(shape) + j);
  }

  int h() const { return std::get<0>(shape); }
  int w() const { return std::get<1>(shape); }
  int c() const { return std::get<2>(shape); }
};

DescriptorField loadMsgpackField(const fs::path &msgpackPath) {
  std::ifstream ifs(msgpackPath);
  std::string buffer((std::istreambuf_iterator<char>(ifs)),
                     std::istreambuf_iterator<char>());
  msgpack::unpacked upd;
  size_t offset = 0;
  std::vector<int> shape =
      msgpack::unpack(buffer.data(), buffer.size(), offset).get().convert();
  std::vector<float> data =
      msgpack::unpack(buffer.data(), buffer.size(), offset).get().convert();

  // std::vector<int> shape;
  // std::vector<float> data;

  if (shape.size() != 3) {
    throw std::runtime_error("desciptor field must have 3 axes");
  }

  if (shape[2] != 256) {
    std::cerr << "Expected 256 channels, got " << shape[2] << std::endl;
  }

  DescriptorField f;
  f.shape = {shape[0], shape[1], shape[2]};
  f.data = Eigen::Map<Eigen::Array<float, Dynamic, Dynamic, RowMajor>>(
      data.data(), shape[0] * shape[1], shape[2]);
  return f;
}

struct Message {
  double u0 = .5, v0 = .5; // cursor position in plot 0
  double heatMin = 0, heatMax = 1;
  int iSlice = 0, jSlice = 0;
  float alpha = 1.0;
  bool exp = false;
  std::chrono::time_point<std::chrono::system_clock> lastHeatmapSwitch;
};

struct State : public Message {
  Array<float, Dynamic, Dynamic, RowMajor> heat;
};

int main(int argc, char *argv[]) {

  AppArgs args(argc, argv);

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
  SafeGlTexture image0(oiioLoadImage(args.image0Path), GL_NEAREST);
  SafeGlTexture image1(oiioLoadImage(args.image1Path), GL_NEAREST);

  const auto desc0 = loadMsgpackField(args.feat0Path);
  const auto desc1 = loadMsgpackField(args.feat1Path);

  Array<float, Dynamic, Dynamic, RowMajor> heatmap;

  State state;
  state.alpha = args.heatmapAlpha;
  state.heat.resize(desc1.h(), desc1.w());

  constexpr auto defaultWindowOptions = ImGuiWindowFlags_NoDecoration |
                                        ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoResize;
  constexpr auto defaultPlotOptions =
      ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased | ImPlotFlags_Crosshairs;

  while (!glfwWindowShouldClose(window)) {
    Message msg{
        .u0 = state.u0,
        .v0 = state.v0,
        .iSlice = state.iSlice,
        .jSlice = state.jSlice,
        .alpha = state.alpha,
        .exp = state.exp,
        .lastHeatmapSwitch = state.lastHeatmapSwitch,
    };

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

      ImGui::Checkbox("exp", &msg.exp);
    }
    ImGui::End();

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
      if (ImPlot::DragPoint("Query", &xyDrag.x, &xyDrag.y, true, queryColor,
                            6)) {
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
          std::max(0, std::min((int)(uv.y * desc0.h()), desc0.h() - 1));
      const auto j =
          std::max(0, std::min((int)(uv.x * desc0.w()), desc0.w() - 1));
      msg.iSlice = i;
      msg.jSlice = j;

      ImPlot::EndPlot();
    }

    ImGui::SameLine();

    if (ImPlot::BeginPlot("Image1", nullptr, nullptr, plotSize,
                          ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased |
                              ImPlotFlags_Crosshairs)) {
      bool cachedSlice = msg.iSlice == state.iSlice &&
                         msg.jSlice == state.jSlice && state.heat.rows() > 0 &&
                         state.heat.cols() > 0 && msg.exp == state.exp;

      if (!cachedSlice) {
        const auto query = desc0(msg.iSlice, msg.jSlice);
        const auto innerProducts =
            (desc1.data.rowwise() * query / std::sqrt(desc1.c()))
                .rowwise()
                .sum()
                .eval();
        Map<Array<float, Dynamic, 1>>(state.heat.data(), desc1.h() * desc1.w())
            << innerProducts;
      }

      if (msg.exp) {
        // ImPlot color interpolation crashes whenever it sees NaNs or
        // infinities
        const auto max = 1e30; // std::numeric_limits<float>::quiet_NaN();
        const auto eH = state.heat.exp();
        heatmap = (eH.isFinite() && (eH < 1e30)).select(eH, max);
      } else {
        heatmap = state.heat;
      }

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
