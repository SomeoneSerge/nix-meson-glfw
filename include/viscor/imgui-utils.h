#ifndef _VISCOR_IMGUI_UTILS_H
#define _VISCOR_IMGUI_UTILS_H

#include <filesystem>
#include <fstream>
#include <string>
#include <torch/torch.h>

#include <imgui.h>
#include <implot.h>

#include "viscor/raii.h"
#include "viscor/utils.h"

namespace VisCor {

namespace fs = std::filesystem;

struct SliceQuery {
  double u0 = 0.5;
  double v0 = 0.5;
  int iSlice = -1;
  int jSlice = -1;
  bool exp = false;
};

inline ImPlotColormap colormapTransparentResample(ImPlotColormap src,
                                                  int newRes, double alpha) {
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

inline double normalizeAlpha(double alpha) {
  return std::max(std::min(std::round(alpha * 100.0) / 100.0, 1.0), 0.0);
}

inline std::string alphaToString(double alpha) {
  return std::to_string(normalizeAlpha(alpha));
}

inline ImPlotColormap colormapTransparentCopy(ImPlotColormap src,
                                              double alpha) {
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

struct ImHeatSlice {
  ImHeatSlice(DescriptorField &&desc0, DescriptorField &&desc1,
              SafeGlTexture &&image0, SafeGlTexture &&image1,
              const torch::Device &device, const bool fix01Scale)
      : fix01Scale(fix01Scale), device(device), desc0(std::move(desc0)),
        desc1(std::move(desc1)), image0(std::move(image0)),
        image1(std::move(image1)) {
    heat = torch::empty(
        {desc1.h(), desc1.w()},
        torch::TensorOptions().device(torch::kCPU).dtype(torch::kFloat32));
    heatOnDevice = torch::zeros(
        {desc1.h(), desc1.w()},
        torch::TensorOptions().device(torch::kCPU).dtype(torch::kF32));
  }

  bool draw() {
    using namespace ImPlot;

    constexpr auto defaultPlotOptions =
        ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased | ImPlotFlags_Crosshairs;

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
      ImPlotPoint xyDrag(newQuery.u0, 1.0 - newQuery.v0);

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
      newQuery.u0 = uv.x;
      newQuery.v0 = uv.y;

      const auto i =
          std::max(0, std::min((int)(uv.y * desc0.h()), desc0.h() - 1));
      const auto j =
          std::max(0, std::min((int)(uv.x * desc0.w()), desc0.w() - 1));
      newQuery.iSlice = i;
      newQuery.jSlice = j;

      ImPlot::EndPlot();
    }

    ImGui::SameLine();

    if (ImPlot::BeginPlot("Image1", nullptr, nullptr, plotSize,
                          ImPlotFlags_NoLegend | ImPlotFlags_AntiAliased |
                              ImPlotFlags_Crosshairs)) {
      bool cachedSlice = newQuery.exp == query.exp &&
                         newQuery.iSlice == query.iSlice &&
                         newQuery.jSlice == query.jSlice && heat.size(0) > 0 &&
                         heat.size(1) > 0;

      if (!cachedSlice) {
        const auto stdvar = std::sqrt(desc1.c());
        const auto query =
            desc0(newQuery.iSlice, newQuery.jSlice).reshape({desc0.c(), 1, 1}) /
            stdvar;
        heatOnDevice = desc1.data.div(stdvar).mul(query).sum(0);
        if (newQuery.exp) {
          heatOnDevice.exp_();
        }
        // ImPlot color interpolation crashes whenever it sees NaNs or
        // infinities
        const auto max = 1e30; // std::numeric_limits<float>::quiet_NaN();
        heatOnDevice.nan_to_num_(max, max, -max);
        heatOnDevice.clip_(-max, max);

        heat.copy_(heatOnDevice.to(torch::kCPU));
      }

      if (fix01Scale) {
        heatMin = 0;
        heatMax = 1;
      } else {
        heatMin = heatOnDevice.min().item<double>();
        heatMax = heatOnDevice.max().item<double>();
      }

      heatMax = std::max(heatMax, heatMin + .1);

      ImPlot::PlotImage("im1", image1.textureVoidStar(), ImPlotPoint(0.0, 0.0),
                        ImPlotPoint(1.0, 1.0));

      const auto cmap = colormapTransparentCopy(ImPlotColormap_Viridis, alpha);
      ImPlot::PushColormap(cmap);
      ImPlot::PlotHeatmap("Correspondence volume slice",
                          (float *)heat.data_ptr(), heat.size(0), heat.size(1),
                          heatMin, heatMax, nullptr);
      ImPlot::PopColormap();

      ImPlot::EndPlot();
    }

    ImPlot::PushColormap(ImPlotColormap_Viridis);
    ImGui::SameLine();
    ImPlot::ColormapScale("ColormapScale", heatMin, heatMax,
                          ImVec2(cmapWidth, plotSize.y));
    ImPlot::PopColormap();

    query = newQuery;

    return true;
  }

  SliceQuery newQuery;
  SliceQuery query;
  bool fix01Scale = false;
  float alpha = .75;
  double heatMin = 0;
  double heatMax = 1;
  torch::Device device;
  DescriptorField desc0;
  DescriptorField desc1;
  SafeGlTexture image0;
  SafeGlTexture image1;
  torch::Tensor heat;
  torch::Tensor heatOnDevice;
};

}; // namespace VisCor

#endif
