/**
 * @file src/rd_cursor.h
 * @brief RD extension: share the host cursor shape so clients can draw it locally.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rd_cursor {
  /// Host advertises this in x-ss-general.featureFlags; client echoes it in x-ml-general.featureFlags.
  constexpr std::uint32_t FEATURE_LOCAL_CURSOR = 0x40000000;
  /// Largest cursor edge sent to clients. Bigger cursors are downscaled.
  constexpr std::uint16_t MAX_EDGE = 96;  // keeps one control message under 64 KiB

  /// Control-stream message types (Sunshine extension range 0x55xx).
  constexpr std::uint16_t MSG_CURSOR_SHAPE = 0x55F0;  ///< u32 id, u16 w, u16 h, u16 hot_x, u16 hot_y, BGRA[w*h]
  constexpr std::uint16_t MSG_CURSOR_STATE = 0x55F1;  ///< u32 id, u8 visible, u8[3] reserved
  /// Client -> host: u8 percent (255 unknown), u8 flags (1 discharging, 2 charging, 4 no battery), u16 minutes left (0xFFFF unknown), UTF-8 device name.
  constexpr std::uint16_t MSG_CLIENT_BATTERY = 0x55F2;

  /**
   * @brief Write the client battery state for the host tray helper.
   * File: %ProgramData%\RD\clients\<instance>.txt (removed when the session ends).
   */
  void write_battery(const std::string &instance, const std::string_view &payload);
  void clear_battery(const std::string &instance);

  struct shape_t {
    std::uint32_t id;  ///< Hash of the image; clients cache by id.
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t hot_x;
    std::uint16_t hot_y;
    std::vector<std::uint8_t> bgra;  ///< Straight-alpha BGRA, width * height * 4 bytes.
  };

  struct snapshot_t {
    std::uint64_t seq;  ///< Changes every time shape or visibility changes.
    std::shared_ptr<const shape_t> shape;
    bool visible;
  };

  /**
   * @brief Publish a new cursor shape from capture.
   * @param alpha Alpha-blend image from make_cursor_alpha_image().
   * @param xor_img XOR image from make_cursor_xor_image() (may be empty).
   * Height is derived from alpha_size / (width * 4).
   */
  void publish_shape(std::uint32_t width, std::uint32_t hot_x, std::uint32_t hot_y, const std::uint8_t *alpha, std::size_t alpha_size, const std::uint8_t *xor_img, std::size_t xor_size);

  /// Publish pointer visibility from capture.
  void publish_visible(bool visible);

  /// Read the latest state (thread safe).
  snapshot_t get();

  /// Number of sessions that draw the cursor on the client. While > 0, capture must not blend the cursor.
  extern std::atomic<int> local_sessions;
}  // namespace rd_cursor
