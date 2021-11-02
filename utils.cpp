#include <regex>

#include "viscor/heatmaps-dir.h"
#include "viscor/utils.h"

using namespace VisCor;

Uint8Image VisCor::oiioLoadImage(const std::string &filename) {
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

VisCor::HeatmapsDir::HeatmapsDir(const fs::path &layoutPath)
    : root(layoutPath.parent_path()) {
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

Matrix<float, Dynamic, Dynamic, RowMajor>
VisCor::HeatmapsDir::slice(int i, int j) const {
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

std::tuple<int, int>
VisCor::HeatmapsDir::indexFromName(const std::string &filename) {
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
