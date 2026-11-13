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
#include <map>
#include <string>
#include <vector>

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


// ---------------------------------------------------------------------------------------------
// Transmit side helpers
// ---------------------------------------------------------------------------------------------

// Build the wire bytes of a frame: 02 | type | payload | 03 | check, escaping 02, 03 and 10 in the body with 10.
// Returns the number of bytes written, or 0 if it does not fit or the check byte would itself need escaping
// (never seen on the bus, so we refuse to send it).
static inline size_t spa_encode_frame(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
  size_t n = 0;
  uint8_t x = type;
  for (size_t i = 0; i < len; i++)
    x ^= payload[i];
  auto put = [&](uint8_t b) -> bool {
    if (b == SPA_STX || b == SPA_ETX || b == SPA_DLE) {
      if (n >= cap)
        return false;
      out[n++] = SPA_DLE;
    }
    if (n >= cap)
      return false;
    out[n++] = b;
    return true;
  };
  if (cap < 1)
    return 0;
  out[n++] = SPA_STX;
  if (!put(type))
    return 0;
  for (size_t i = 0; i < len; i++)
    if (!put(payload[i]))
      return 0;
  if (n + 2 > cap)
    return 0;
  if (x == SPA_STX || x == SPA_ETX || x == SPA_DLE)
    return 0;
  out[n++] = SPA_ETX;
  out[n++] = x;
  return n;
}

// Key frame (type 80) with the three payload bytes seen on the bus.
static inline size_t spa_encode_key(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t *out, size_t cap) {
  const uint8_t payload[3] = {b0, b1, b2};
  return spa_encode_frame(0x80, payload, 3, out, cap);
}

enum class SpaKey : uint8_t { PUMP1 = 0, PUMP2 = 1, AUX = 2 };

// Key payload bytes 0 and 1 for the keys we can send (byte 2 is 01 on every key-down seen).
static inline void spa_key_bytes(SpaKey k, uint8_t *b0, uint8_t *b1) {
  switch (k) {
    case SpaKey::PUMP1:
      *b0 = 0x10;
      *b1 = 0x00;
      break;
    case SpaKey::PUMP2:
      *b0 = 0x20;
      *b1 = 0x00;
      break;
    case SpaKey::AUX:
      *b0 = 0x00;
      *b1 = 0x04;
      break;
  }
}

// What the controller currently reports (decoded from the status frame and the display text).
struct SpaState {
  bool known{false};    // at least one status frame decoded
  uint8_t pump1{0};     // 0 off, 1 auto, 2 on (the pump key cycles off -> auto -> on -> off)
  bool pump2{false};
  bool aux{false};
  bool ui_known{false};  // at least one display frame seen, so ui_busy is meaningful
  bool ui_busy{false};   // a menu, edit or adjust screen is showing: pump and aux keys may mean something else
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
  bool has_light_color{false};
  std::string light_color;
  bool has_pump_running{false};
  bool pump_running{false};
  bool has_heater{false};
  bool heater{false};
  bool has_ozone{false};
  bool ozone{false};
  bool has_pump2_running{false};
  bool pump2_running{false};
  bool has_aux{false};
  bool aux{false};
  // Human readable change log lines (status bit diffs, other frame changes, key presses) for state mapping.
  std::vector<std::string> notes;
};

class SpaDecoder {
 public:
  const SpaState &state() const { return state_; }

  void process(const SpaFrame &f, SpaUpdate &u) {
    switch (f.type) {
      case 0x22:  // display line 1 (15 ASCII chars); normally starts a display burst
        line1_ = ascii_(f.payload, f.len);
        line2_.clear();
        have_line2_ = false;
        state_.ui_known = true;
        state_.ui_busy = line1_.find("(^)") != std::string::npos;  // menu navigation line
        handle_line1_(f, u);
        break;
      case 0x27:  // takes the place of line 1 on the LIGHT ADJUST screen
      case 0x23:  // seen at the start of setpoint edit mode
        line1_.clear();
        line2_.clear();
        have_line2_ = false;
        state_.ui_known = true;
        state_.ui_busy = true;
        break;
      case 0x21: {  // display line 2
        std::string raw = ascii_(f.payload, f.len);
        line2_ = raw;
        have_line2_ = true;
        {
          std::string up2 = upper_(raw);
          if (up2.find("LIGHT MODE") != std::string::npos || up2.find("LIGHT ADJUST") != std::string::npos ||
              up2.find("MAIN MENU") != std::string::npos || up2.find("SET TEMP") != std::string::npos)
            state_.ui_busy = true;
        }
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
      case 0x31:
        handle_color_(f, u);
        break;
      case 0x80:
        handle_key_(f, u);
        break;
      default:
        break;
    }
    // Every type except the ones with dedicated entities is change-logged for state mapping.
    if (f.type != 0x21 && f.type != 0x22 && f.type != 0x25 && f.type != 0x30 && f.type != 0x80)
      note_other_(f, u);
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

  // "b2 03->43 (+40)": byte index, old->new, and the bits that turned on (+) or off (-).
  static std::string diff_(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    std::string out;
    size_t n = an < bn ? an : bn;
    char buf[48];
    for (size_t i = 0; i < n; i++) {
      if (a[i] == b[i])
        continue;
      uint8_t on = b[i] & ~a[i], off = a[i] & ~b[i];
      snprintf(buf, sizeof(buf), "b%u %02x->%02x (", (unsigned) i, a[i], b[i]);
      if (!out.empty())
        out += ", ";
      out += buf;
      if (on) {
        snprintf(buf, sizeof(buf), "+%02x", on);
        out += buf;
      }
      if (off) {
        snprintf(buf, sizeof(buf), "%s-%02x", on ? " " : "", off);
        out += buf;
      }
      out += ")";
    }
    if (an != bn)
      out += (out.empty() ? "" : ", ") + std::string("length changed");
    return out;
  }

  void note_other_(const SpaFrame &f, SpaUpdate &u) {
    std::string now = f.len ? hex_(f.payload, f.len) : std::string("(empty)");
    auto it = last_other_.find(f.type);
    char buf[24];
    snprintf(buf, sizeof(buf), "type %02x: ", f.type);
    if (it == last_other_.end()) {
      u.notes.push_back(std::string(buf) + now + " (first seen)");
    } else if (it->second != now) {
      u.notes.push_back(std::string(buf) + it->second + " -> " + now);
    }
    last_other_[f.type] = now;
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
    if (pub_status_ && raw != last_status_raw_)
      u.notes.push_back("status change: " + diff_(last_status_bytes_, last_status_len_, f.payload, f.len));
    memcpy(last_status_bytes_, f.payload, f.len);
    last_status_len_ = f.len;
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
    // Output bits in the status frame. Mapped from a live capture out of demo mode (one run):
    //   pump key OFF -> AUTO -> ON: b2 bit 0x02 and b3 bit 0x10 mean the pump is running, b3 bit 0x04 means AUTO.
    //   About 6 s after the pump came on: b1 bit 0x08 = heater (heating LED). About 3 s later: b2 bit 0x40 = ozone.
    bool pump_running = (f.payload[2] & 0x02) && (f.payload[3] & 0x10);
    const char *pump = !pump_running ? "off" : ((f.payload[3] & 0x04) ? "auto" : "on");
    if (last_pump_ != pump || !pub_pump_) {
      last_pump_ = pump;
      pub_pump_ = true;
      u.has_pump = true;
      u.pump = pump;
    }
    if (!pub_pump_running_ || pump_running != last_pump_running_) {
      pub_pump_running_ = true;
      last_pump_running_ = pump_running;
      u.has_pump_running = true;
      u.pump_running = pump_running;
    }
    bool heater = (f.payload[1] & 0x08) != 0;
    if (!pub_heater_ || heater != last_heater_) {
      pub_heater_ = true;
      last_heater_ = heater;
      u.has_heater = true;
      u.heater = heater;
    }
    bool ozone = (f.payload[2] & 0x40) != 0;
    if (!pub_ozone_ || ozone != last_ozone_) {
      pub_ozone_ = true;
      last_ozone_ = ozone;
      u.has_ozone = true;
      u.ozone = ozone;
    }
    // Second pump (display "PUMP ON" / "PUMP OFF", two states): b2 bit 0x01 and b3 bit 0x20.
    bool pump2 = (f.payload[2] & 0x01) && (f.payload[3] & 0x20);
    if (!pub_pump2_ || pump2 != last_pump2_) {
      pub_pump2_ = true;
      last_pump2_ = pump2;
      u.has_pump2_running = true;
      u.pump2_running = pump2;
    }
    // AUX (display "AUX ON" / "AUX OFF"): b2 bit 0x80.
    bool aux = (f.payload[2] & 0x80) != 0;
    state_.known = true;
    state_.pump1 = !pump_running ? 0 : ((f.payload[3] & 0x04) ? 1 : 2);
    state_.pump2 = pump2;
    state_.aux = aux;
    if (!pub_aux_ || aux != last_aux_) {
      pub_aux_ = true;
      last_aux_ = aux;
      u.has_aux = true;
      u.aux = aux;
    }
  }

  // EXPERIMENTAL: three bytes that sit at 55 55 55 normally and change while a colour-changing light mode runs
  // (seen ff 00 00, 00 00 ff, 80 00 80, 00 55 aa). Byte order (RGB or another order) is not verified.
  void handle_color_(const SpaFrame &f, SpaUpdate &u) {
    if (f.len < 3)
      return;
    char buf[12];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", f.payload[0], f.payload[1], f.payload[2]);
    std::string c = buf;
    if (c != last_color_ || !pub_color_) {
      last_color_ = c;
      pub_color_ = true;
      u.has_light_color = true;
      u.light_color = c;
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
    else if (b1 == 0 && b0 == 0x20)
      name = "pump2";  // display shows PUMP ON / PUMP OFF
    else if (b1 == 0 && b0 == 0x40)
      name = "light_mode";
    else if (b0 == 0 && b1 == 0x01)
      name = "light";
    else if (b0 == 0 && b1 == 0x04)
      name = "aux";  // display shows AUX ON / AUX OFF
    else {
      char buf[24];
      snprintf(buf, sizeof(buf), "unknown_%02x_%02x", b0, b1);
      name = buf;
    }
    if (name != "none" && (name != last_key_ || last_key_ == "none"))
      u.notes.push_back("key " + name);
    if (name != last_key_ || !pub_key_) {
      last_key_ = name;
      pub_key_ = true;
      u.has_key = true;
      u.key = name;
    }
  }

  SpaState state_;
  std::string line1_, line2_, last_line1_, last_line2_, last_status_raw_, last_key_, last_pump_, last_color_;
  bool have_line2_{false};
  bool pub_line1_{false}, pub_line2_{false}, pub_set_{false}, pub_current_{false}, pub_light_{false},
      pub_status_{false}, pub_pump_{false}, pub_key_{false}, pub_color_{false}, pub_pump_running_{false},
      pub_heater_{false}, pub_ozone_{false}, pub_pump2_{false}, pub_aux_{false};
  bool last_pump_running_{false}, last_heater_{false}, last_ozone_{false}, last_pump2_{false}, last_aux_{false};
  float last_set_{0}, last_current_{0}, last_light_{0};
  uint8_t last_status_bytes_[SPA_MAX_BODY]{};
  size_t last_status_len_{0};
  std::map<uint8_t, std::string> last_other_;
};


// Presses keys on request. All timing comes from the caller (milliseconds and loop gap), so it can be tested on a PC.
//
// A key press is two frames, like the real keypad: key-down, then about 150 ms later the release frame
// (payload 00 00 00). Each frame may only start in the reply slot right after a status (type 30) frame:
// the real keypad starts 0 to 2 ms after the status frame ends and the controller waits for it, so
// on_slot() only transmits when the caller says it is being called within max_latency_us of the frame end.
class SpaTxSequencer {
 public:
  void configure(uint32_t hold_ms, uint32_t response_timeout_ms, uint32_t max_latency_us) {
    hold_ms_ = hold_ms;
    response_timeout_ms_ = response_timeout_ms;
    max_latency_us_ = max_latency_us;
  }

  // Number of key presses needed to reach the target pump 1 mode (the key cycles off -> auto -> on -> off).
  static int pump1_presses(uint8_t current, uint8_t target) { return (3 + (int) target - (int) current) % 3; }

  bool request_pump1(uint8_t target, const SpaState &st, uint32_t now) {
    if (!precheck_(st))
      return false;
    if (target > 2) {
      error_ = "bad pump mode";
      return false;
    }
    if (st.pump1 == target)
      return true;
    start_(Goal::PUMP1, SpaKey::PUMP1, target, 5, now);
    return true;
  }

  bool request_toggle(SpaKey key, bool target, const SpaState &st, uint32_t now) {
    if (!precheck_(st))
      return false;
    if (key == SpaKey::PUMP1) {
      error_ = "use request_pump1";
      return false;
    }
    bool cur = key == SpaKey::PUMP2 ? st.pump2 : st.aux;
    if (cur == target)
      return true;
    start_(key == SpaKey::PUMP2 ? Goal::PUMP2 : Goal::AUX, key, target ? 1 : 0, 3, now);
    return true;
  }

  // One raw press with no target (for the button entities).
  bool press_raw(SpaKey key, const SpaState &st, uint32_t now) {
    if (!precheck_(st))
      return false;
    start_(Goal::RAW, key, 0, 1, now);
    return true;
  }

  bool busy() const { return phase_ != IDLE; }
  const char *error() const { return error_; }
  uint32_t late_skips() const { return late_skips_; }
  uint32_t frames_sent() const { return frames_sent_; }

  bool take_result(std::string *msg) {
    if (!have_result_)
      return false;
    have_result_ = false;
    *msg = result_;
    return true;
  }
  bool last_ok() const { return last_ok_; }

  // Call every loop pass.
  void tick(uint32_t now, const SpaState &st) { update_(now, st); }

  // Call when a type 30 frame has just completed and nothing else has arrived yet. loop_gap_us is how long it was
  // since the previous loop pass: the frame ended at most that long ago. Returns true if out[0..*len) must be sent now.
  bool on_slot(uint32_t now, uint32_t loop_gap_us, const SpaState &st, uint8_t *out, size_t cap, size_t *len) {
    update_(now, st);
    if (phase_ != NEED_DOWN && phase_ != NEED_UP)
      return false;
    if (loop_gap_us > max_latency_us_) {
      late_skips_++;
      return false;
    }
    if (phase_ == NEED_UP) {
      *len = spa_encode_key(0, 0, 0, out, cap);
      if (*len == 0)
        return false;
      set_phase_(AWAIT, now);
      frames_sent_++;
      return true;
    }
    // NEED_DOWN
    if ((uint32_t) (now - last_done_ms_) < COOLDOWN_MS)
      return false;
    if (goal_ != Goal::RAW && satisfied_(st)) {  // state changed by itself while we waited
      finish_ok_(now);
      return false;
    }
    if (!st.ui_known || st.ui_busy)
      return false;  // wait for a display frame; update_() gives up after SLOT_TIMEOUT_MS
    uint8_t b0 = 0, b1 = 0;
    spa_key_bytes(key_, &b0, &b1);
    *len = spa_encode_key(b0, b1, 0x01, out, cap);
    if (*len == 0) {
      fail_("cannot encode key frame");
      return false;
    }
    before_ = st;
    effect_ = false;
    t_down_ = now;
    attempts_left_--;
    presses_++;
    set_phase_(HOLD, now);
    frames_sent_++;
    return true;
  }

 protected:
  enum Phase : uint8_t { IDLE, NEED_DOWN, HOLD, NEED_UP, AWAIT };
  enum class Goal : uint8_t { NONE, PUMP1, PUMP2, AUX, RAW };
  static const uint32_t COOLDOWN_MS = 300;
  static const uint32_t BUSY_TIMEOUT_MS = 5000;
  static const uint32_t SLOT_TIMEOUT_MS = 5000;

  bool precheck_(const SpaState &st) {
    if (phase_ != IDLE) {
      error_ = "another key action is still running";
      return false;
    }
    if (!st.known) {
      error_ = "no status frame seen yet";
      return false;
    }
    error_ = "";
    return true;
  }

  void start_(Goal g, SpaKey key, uint8_t target, uint8_t attempts, uint32_t now) {
    goal_ = g;
    key_ = key;
    target_ = target;
    attempts_left_ = attempts;
    presses_ = 0;
    started_ms_ = now;
    last_done_ms_ = now - COOLDOWN_MS;  // first press may go straight away
    set_phase_(NEED_DOWN, now);
  }

  void set_phase_(Phase p, uint32_t now) {
    phase_ = p;
    phase_since_ms_ = now;
    skips_at_phase_start_ = late_skips_;
  }

  bool satisfied_(const SpaState &st) const {
    switch (goal_) {
      case Goal::PUMP1:
        return st.pump1 == target_;
      case Goal::PUMP2:
        return st.pump2 == (target_ != 0);
      case Goal::AUX:
        return st.aux == (target_ != 0);
      default:
        return true;
    }
  }

  bool changed_(const SpaState &st) const {
    switch (key_) {
      case SpaKey::PUMP1:
        return st.pump1 != before_.pump1;
      case SpaKey::PUMP2:
        return st.pump2 != before_.pump2;
      case SpaKey::AUX:
        return st.aux != before_.aux;
    }
    return false;
  }

  void update_(uint32_t now, const SpaState &st) {
    switch (phase_) {
      case NEED_DOWN:
      case NEED_UP:
        if ((uint32_t) (now - phase_since_ms_) > SLOT_TIMEOUT_MS) {
          if (late_skips_ != skips_at_phase_start_)
            fail_("no reply slot was fast enough (loop too slow)");
          else if (!st.ui_known)
            fail_("no display frame seen, cannot tell if a menu is open");
          else if (st.ui_busy)
            fail_("keypad menu is open, not sending");
          else
            fail_("no reply slot available");
        }
        break;
      case HOLD:
        if (!effect_ && changed_(st))
          effect_ = true;
        if ((uint32_t) (now - t_down_) >= hold_ms_)
          set_phase_(NEED_UP, now);
        break;
      case AWAIT:
        if (!effect_ && changed_(st))
          effect_ = true;
        if (effect_) {
          last_done_ms_ = now;
          if (goal_ == Goal::RAW || satisfied_(st))
            finish_ok_(now);
          else if (attempts_left_ > 0)
            set_phase_(NEED_DOWN, now);
          else
            fail_("target state not reached");
        } else if ((uint32_t) (now - t_down_) >= response_timeout_ms_) {
          last_done_ms_ = now;
          if (attempts_left_ > 0)
            set_phase_(NEED_DOWN, now);  // no response: try again
          else
            fail_("no response to the key press");
        }
        break;
      default:
        break;
    }
  }

  void finish_ok_(uint32_t now) {
    char buf[64];
    snprintf(buf, sizeof(buf), "done after %d press(es), %u ms", presses_, (unsigned) (now - started_ms_));
    result_ = buf;
    last_ok_ = true;
    have_result_ = true;
    phase_ = IDLE;
  }

  void fail_(const char *why) {
    result_ = why;
    error_ = why;
    last_ok_ = false;
    have_result_ = true;
    phase_ = IDLE;
  }

  Phase phase_{IDLE};
  Goal goal_{Goal::NONE};
  SpaKey key_{SpaKey::PUMP1};
  uint8_t target_{0};
  int presses_{0};
  int attempts_left_{0};
  uint32_t hold_ms_{150}, response_timeout_ms_{800}, max_latency_us_{1500};
  uint32_t started_ms_{0}, t_down_{0}, last_done_ms_{0}, phase_since_ms_{0}, skips_at_phase_start_{0};
  bool effect_{false};
  SpaState before_;
  const char *error_{""};
  std::string result_;
  bool have_result_{false}, last_ok_{false};
  uint32_t late_skips_{0}, frames_sent_{0};
};

}  // namespace spa_power
}  // namespace esphome
