#pragma once
// Pure C++ (no ESPHome dependencies) so the protocol code can be unit-tested on a PC.
//
// Bus format (19200 baud, 8N1, TTL levels carried over RS485 transceivers):
//   02 | type | payload ... | 03 | check
// check = XOR of type and payload bytes AFTER unescaping.
// 0x10 (DLE) escapes the next byte: a literal 02, 03 or 10 inside the body is sent as 10 02, 10 03, 10 10.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace esphome {
namespace spa_power {

static const uint8_t SPA_STX = 0x02;
static const uint8_t SPA_ETX = 0x03;
static const uint8_t SPA_DLE = 0x10;
static const size_t SPA_MAX_BODY = 48;  // type + payload

struct SpaFrame {
  uint8_t type{0};
  uint8_t payload[SPA_MAX_BODY]{};
  size_t len{0};  // payload length, excludes the type byte
};

class SpaFrameParser {
 public:
  // Feed one received byte. Returns true when a complete, checksum-valid frame is available via frame().
  bool feed(uint8_t b) {
    switch (state_) {
      case WAIT_STX:
        if (b == SPA_STX)
          start_();
        return false;
      case BODY:
        if (escaped_) {
          escaped_ = false;
          push_(b);
          return false;
        }
        if (b == SPA_DLE) {
          escaped_ = true;
          return false;
        }
        if (b == SPA_STX) {  // unescaped STX inside a body means we lost sync: restart here
          this->resyncs_++;
          start_();
          return false;
        }
        if (b == SPA_ETX) {
          state_ = CHECK;
          return false;
        }
        push_(b);
        return false;
      case CHECK:
        if (b == xor_)
          return finish_();
        if (b == SPA_DLE) {  // check byte itself may be escaped (not seen in captures; handled defensively)
          state_ = CHECK_ESC;
          return false;
        }
        reject_();
        if (b == SPA_STX)
          start_();
        return false;
      case CHECK_ESC:
        if (b == xor_)
          return finish_();
        reject_();
        return false;
    }
    return false;
  }

  const SpaFrame &frame() const { return frame_; }
  uint32_t frames_ok() const { return ok_; }
  uint32_t frames_bad() const { return bad_; }
  uint32_t resyncs() const { return resyncs_; }
  uint32_t overflows() const { return overflows_; }

 protected:
  enum State { WAIT_STX, BODY, CHECK, CHECK_ESC };

  void start_() {
    state_ = BODY;
    escaped_ = false;
    len_ = 0;
    xor_ = 0;
  }
  void push_(uint8_t b) {
    if (len_ >= sizeof(body_)) {
      overflows_++;
      state_ = WAIT_STX;
      return;
    }
    body_[len_++] = b;
    xor_ ^= b;
  }
  void reject_() {
    bad_++;
    state_ = WAIT_STX;
  }
  bool finish_() {
    state_ = WAIT_STX;
    if (len_ == 0) {
      bad_++;
      return false;
    }
    frame_.type = body_[0];
    frame_.len = len_ - 1;
    if (frame_.len > 0)
      memcpy(frame_.payload, body_ + 1, frame_.len);
    ok_++;
    return true;
  }

  State state_{WAIT_STX};
  bool escaped_{false};
  uint8_t body_[SPA_MAX_BODY]{};
  size_t len_{0};
  uint8_t xor_{0};
  SpaFrame frame_;
  uint32_t ok_{0}, bad_{0}, resyncs_{0}, overflows_{0};
};

// Values that changed because of one frame. Only fields with has_* set should be published.
struct SpaUpdate {
  bool has_current_temp{false};
  float current_temp{0};
  bool has_set_temp{false};
  float set_temp{0};
  bool has_light{false};
  float light_percent{0};
  bool has_line1{false};
  std::string line1;
  bool has_line2{false};
  std::string line2;
  bool has_key{false};
  std::string key;
  bool has_pump{false};
  std::string pump;
  bool has_status_raw{false};
  std::string status_raw;
};

class SpaDecoder {
 public:
  void process(const SpaFrame &f, SpaUpdate &u) {
    switch (f.type) {
      case 0x22:  // display line 1 (15 ASCII chars); normally starts a display burst
        line1_ = ascii_(f.payload, f.len);
        line2_.clear();
        have_line2_ = false;
        handle_line1_(f, u);
        break;
      case 0x27:  // takes the place of line 1 on the LIGHT ADJUST screen
      case 0x23:  // seen at the start of setpoint edit mode
        line1_.clear();
        line2_.clear();
        have_line2_ = false;
        break;
      case 0x21: {  // display line 2
        std::string raw = ascii_(f.payload, f.len);
        line2_ = raw;
        have_line2_ = true;
        std::string t = trim_(raw);
        if (t != last_line2_ || !pub_line2_) {
          last_line2_ = t;
          pub_line2_ = true;
          u.has_line2 = true;
          u.line2 = t;
        }
        break;
      }
      case 0x25:
        handle_temperature_(f, u);
        break;
      case 0x30:
        handle_status_(f, u);
        break;
      case 0x80:
        handle_key_(f, u);
        break;
      default:
        break;
    }
  }

 protected:
  static std::string ascii_(const uint8_t *p, size_t n) {
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; i++) {
      uint8_t c = p[i];
      if (c == 0x1e)
        s += '^';  // up-arrow glyph
      else if (c == 0x1f)
        s += 'v';  // down-arrow glyph
      else if (c >= 0x20 && c < 0x7f)
        s += (char) c;
      else
        s += '?';
    }
    return s;
  }
  static std::string trim_(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] == ' ')
      a++;
    while (b > a && s[b - 1] == ' ')
      b--;
    return s.substr(a, b - a);
  }
  static std::string upper_(std::string s) {
    for (auto &c : s)
      if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    return s;
  }
  static std::string hex_(const uint8_t *p, size_t n) {
    std::string s;
    char buf[4];
    for (size_t i = 0; i < n; i++) {
      snprintf(buf, sizeof(buf), "%02x", p[i]);
      if (i)
        s += ' ';
      s += buf;
    }
    return s;
  }

  void handle_line1_(const SpaFrame &f, SpaUpdate &u) {
    std::string raw = ascii_(f.payload, f.len);
    std::string t = trim_(raw);
    if (t != last_line1_ || !pub_line1_) {
      last_line1_ = t;
      pub_line1_ = true;
      u.has_line1 = true;
      u.line1 = t;
    }
    // "Set temp  30.50": the setpoint is only sent as text on the normal screens.
    std::string up = upper_(raw);
    size_t pos = up.find("SET TEMP");
    if (pos == 0) {
      const char *s = raw.c_str() + 8;
      char *end = nullptr;
      float v = strtof(s, &end);
      if (end != s && v > 0.0f && v < 100.0f)
        emit_set_(v, u);
    }
  }

  void emit_set_(float v, SpaUpdate &u) {
    if (!pub_set_ || fabsf(v - last_set_) > 0.001f) {
      pub_set_ = true;
      last_set_ = v;
      u.has_set_temp = true;
      u.set_temp = v;
    }
  }

  void handle_temperature_(const SpaFrame &f, SpaUpdate &u) {
    if (f.len < 2)
      return;
    float v = f.payload[0] + f.payload[1] / 100.0f;
    // On the SET TEMP menu item (line 1 "(^) (SET) (v)", line 2 "1. SET TEMP") and in setpoint edit mode
    // (same line 1, blank line 2) this frame carries the setpoint. On every other menu it is not used.
    // On the normal screens it is the current temperature.
    bool menu_line = line1_.find("(^)") != std::string::npos;
    if (menu_line) {
      bool set_item = line1_.find("(SET)") != std::string::npos &&
                      (upper_(line2_).find("SET TEMP") != std::string::npos || (have_line2_ && trim_(line2_).empty()));
      if (set_item)
        emit_set_(v, u);
      return;
    }
    if (!pub_current_ || fabsf(v - last_current_) > 0.001f) {
      pub_current_ = true;
      last_current_ = v;
      u.has_current_temp = true;
      u.current_temp = v;
    }
  }

  void handle_status_(const SpaFrame &f, SpaUpdate &u) {
    if (f.len < 11)
      return;
    std::string raw = hex_(f.payload, f.len);
    if (raw != last_status_raw_ || !pub_status_) {
      last_status_raw_ = raw;
      pub_status_ = true;
      u.has_status_raw = true;
      u.status_raw = raw;
    }
    // Byte 4 is the light brightness, 0..255 (checked against the percentage in the type 27 frame).
    float pct = roundf(f.payload[4] * 100.0f / 255.0f);
    if (!pub_light_ || fabsf(pct - last_light_) > 0.5f) {
      pub_light_ = true;
      last_light_ = pct;
      u.has_light = true;
      u.light_percent = pct;
    }
    // EXPERIMENTAL: seen only for one pump (OFF / AUTO / LOW) in a handful of captures.
    bool on = (f.payload[2] & 0x03) == 0x03 && (f.payload[3] & 0x30) == 0x30;
    const char *pump = !on ? "off" : ((f.payload[3] & 0x04) ? "auto" : "low");
    if (last_pump_ != pump || !pub_pump_) {
      last_pump_ = pump;
      pub_pump_ = true;
      u.has_pump = true;
      u.pump = pump;
    }
  }

  void handle_key_(const SpaFrame &f, SpaUpdate &u) {
    if (f.len < 3)
      return;
    uint8_t b0 = f.payload[0], b1 = f.payload[1];
    std::string name;
    if (b0 == 0 && b1 == 0)
      name = "none";
    else if (b1 == 0 && b0 == 0x01)
      name = "down";
    else if (b1 == 0 && b0 == 0x02)
      name = "menu_set";
    else if (b1 == 0 && b0 == 0x04)
      name = "up";
    else if (b1 == 0 && b0 == 0x10)
      name = "pump1";
    else if (b1 == 0 && b0 == 0x40)
      name = "light_mode";
    else if (b0 == 0 && b1 == 0x01)
      name = "light";
    else if (b0 == 0 && b1 == 0x02)
      name = "pump2a";  // label as given; no response seen on this hardware
    else {
      char buf[24];
      snprintf(buf, sizeof(buf), "unknown_%02x_%02x", b0, b1);
      name = buf;
    }
    if (name != last_key_ || !pub_key_) {
      last_key_ = name;
      pub_key_ = true;
      u.has_key = true;
      u.key = name;
    }
  }

  std::string line1_, line2_, last_line1_, last_line2_, last_status_raw_, last_key_, last_pump_;
  bool have_line2_{false};
  bool pub_line1_{false}, pub_line2_{false}, pub_set_{false}, pub_current_{false}, pub_light_{false},
      pub_status_{false}, pub_pump_{false}, pub_key_{false};
  float last_set_{0}, last_current_{0}, last_light_{0};
};

}  // namespace spa_power
}  // namespace esphome
