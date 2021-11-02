#ifndef _VISCOR_HEATMAPS_DIR_H
#define _VISCOR_HEATMAPS_DIR_H

#include "viscor/utils.h"
#include <Eigen/Dense>
#include <filesystem>

using namespace Eigen;

namespace VisCor {

struct HeatmapsDir {
  HeatmapsDir(const fs::path &layoutPath);
  static std::tuple<int, int> indexFromName(const std::string &filename);

  std::optional<fs::path> slicePath(int i, int j) const {
    return slices[sliceIndices(i, j)];
  }

  Matrix<float, Dynamic, Dynamic, RowMajor> slice(int i, int j) const;

  int h0, w0, h1, w1;
  fs::path root;
  std::vector<fs::path> slices;
  Eigen::Array<int, Dynamic, Dynamic, RowMajor> sliceIndices;
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
}; // namespace VisCor
#endif
