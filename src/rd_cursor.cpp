/**
 * @file src/rd_cursor.cpp
 * @brief RD extension: share the host cursor shape so clients can draw it locally.
 */
#include "rd_cursor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace rd_cursor {
  std::atomic<int> local_sessions {0};

  namespace {
    std::mutex mtx;
    snapshot_t state {0, nullptr, false};

    std::uint32_t fnv1a(const std::vector<std::uint8_t> &data, std::uint32_t seed) {
      std::uint32_t h = 2166136261u ^ seed;
      for (auto b : data) {
        h = (h ^ b) * 16777619u;
      }
      return h ? h : 1;
    }
  }  // namespace

  void publish_shape(std::uint32_t width, std::uint32_t hot_x, std::uint32_t hot_y, const std::uint8_t *alpha, std::size_t alpha_size, const std::uint8_t *xor_img, std::size_t xor_size) {
    if (width == 0 || !alpha || alpha_size < (std::size_t) width * 4) {
      return;
    }
    std::uint32_t height = (std::uint32_t) (alpha_size / ((std::size_t) width * 4));

    // Merge: alpha image as-is; XOR pixels (inverting, e.g. text I-beam) become opaque black,
    // since client OS cursors cannot XOR with the stream image.
    std::vector<std::uint8_t> merged(alpha, alpha + (std::size_t) width * height * 4);
    if (xor_img && xor_size >= merged.size()) {
      for (std::size_t i = 0; i < merged.size(); i += 4) {
        const bool xor_on = xor_img[i] | xor_img[i + 1] | xor_img[i + 2];
        if (xor_on && xor_img[i + 3]) {
          merged[i] = merged[i + 1] = merged[i + 2] = 0x00;
          merged[i + 3] = 0xFF;
        }
      }
    }

    // Downscale (nearest neighbour) if too big.
    auto shape = std::make_shared<shape_t>();
    const std::uint32_t edge = std::max(width, height);
    if (edge > MAX_EDGE) {
      const double s = (double) MAX_EDGE / edge;
      const std::uint32_t nw = std::max<std::uint32_t>(1, (std::uint32_t) (width * s));
      const std::uint32_t nh = std::max<std::uint32_t>(1, (std::uint32_t) (height * s));
      std::vector<std::uint8_t> scaled((std::size_t) nw * nh * 4);
      for (std::uint32_t y = 0; y < nh; ++y) {
        for (std::uint32_t x = 0; x < nw; ++x) {
          const std::uint32_t sx = std::min(width - 1, (std::uint32_t) (x / s));
          const std::uint32_t sy = std::min(height - 1, (std::uint32_t) (y / s));
          std::copy_n(&merged[((std::size_t) sy * width + sx) * 4], 4, &scaled[((std::size_t) y * nw + x) * 4]);
        }
      }
      merged = std::move(scaled);
      hot_x = (std::uint32_t) (hot_x * s);
      hot_y = (std::uint32_t) (hot_y * s);
      width = nw;
      height = nh;
    }

    shape->width = (std::uint16_t) width;
    shape->height = (std::uint16_t) height;
    shape->hot_x = (std::uint16_t) std::min(hot_x, width - 1);
    shape->hot_y = (std::uint16_t) std::min(hot_y, height - 1);
    shape->id = fnv1a(merged, (width << 16) ^ height ^ (shape->hot_x << 8) ^ (shape->hot_y << 24));
    shape->bgra = std::move(merged);

    std::lock_guard lg {mtx};
    if (state.shape && state.shape->id == shape->id) {
      return;
    }
    state.shape = std::move(shape);
    ++state.seq;
  }

  void publish_visible(bool visible) {
    std::lock_guard lg {mtx};
    if (state.visible != visible) {
      state.visible = visible;
      ++state.seq;
    }
  }

  snapshot_t get() {
    std::lock_guard lg {mtx};
    return state;
  }

  namespace {
    std::filesystem::path battery_path(const std::string &instance) {
      const char *pd = std::getenv("ProgramData");
      std::filesystem::path dir = std::filesystem::path(pd ? pd : "C:/ProgramData") / "RD" / "clients";
      std::string safe;
      for (char c : instance) {
        safe += (std::isalnum((unsigned char) c) || c == '-' || c == '_') ? c : '_';
      }
      return dir / (safe.empty() ? "default.txt" : safe + ".txt");
    }
  }  // namespace

  void write_battery(const std::string &instance, const std::string_view &payload) {
    if (payload.size() < 4) {
      return;
    }
    auto p = (const std::uint8_t *) payload.data();
    unsigned percent = p[0];
    unsigned flags = p[1];
    unsigned minutes = p[2] | (p[3] << 8);
    std::string name;
    for (std::size_t i = 4; i < payload.size() && name.size() < 64; ++i) {
      char c = payload[i];
      if (c != '\n' && c != '\r' && c != '\0') {
        name += c;
      }
    }

    std::error_code ec;
    auto path = battery_path(instance);
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
      std::ofstream f(tmp, std::ios::trunc);
      if (!f) {
        return;
      }
      auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      f << "instance=" << instance << '\n'
        << "name=" << name << '\n'
        << "percent=" << percent << '\n'
        << "flags=" << flags << '\n'
        << "minutes=" << minutes << '\n'
        << "time=" << now << '\n';
    }
    std::filesystem::rename(tmp, path, ec);
  }

  void clear_battery(const std::string &instance) {
    std::error_code ec;
    std::filesystem::remove(battery_path(instance), ec);
  }
}  // namespace rd_cursor
