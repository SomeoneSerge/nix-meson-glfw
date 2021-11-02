#define GLEW_STATIC

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

#include "viscor/heatmaps-dir.h"
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
  bool exp = false;
  std::chrono::time_point<std::chrono::system_clock> lastHeatmapSwitch;
};

struct State : public Message {
  Matrix<float, Dynamic, Dynamic, RowMajor> heat;
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
  SafeGlTexture image0(oiioLoadImage(args.image0Path), GL_NEAREST);
  SafeGlTexture image1(oiioLoadImage(args.image1Path), GL_NEAREST);

  State state;
  state.alpha = args.heatmapAlpha;

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
        .idVolume = state.idVolume,
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

      ImGui::ListBox("Heatmaps", &msg.idVolume, trace.namesCStr.data(),
                     trace.namesCStr.size(), 2);
      ImGui::Checkbox("exp", &msg.exp);
    }
    ImGui::End();

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
      const auto t = std::chrono::system_clock::now();
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          t - state.lastHeatmapSwitch);

      constexpr auto mod = [](auto a, auto b) { return ((a % b) + b) % b; };

      if (ms.count() > 300) {
        const int add =
            (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? -1 : +1;
        msg.idVolume = mod(msg.idVolume + add, (long)trace.volumes.size());
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

      const auto heatmap =
          msg.exp ? state.heat.array().exp().matrix().eval() : state.heat;

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
