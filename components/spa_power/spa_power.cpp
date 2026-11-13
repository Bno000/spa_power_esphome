#include "spa_power.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace spa_power {

static const char *const TAG = "spa_power";

static std::string hex_string(const uint8_t *p, size_t n) {
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

void SpaPowerComponent::setup() {
  this->last_stats_ms_ = millis();
  this->last_pass_end_us_ = micros();
  this->seq_.configure(this->key_hold_ms_, this->response_timeout_ms_, this->max_reply_latency_us_, this->key_spacing_ms_);
  this->setpoint_.configure(200);
}

void SpaPowerComponent::loop() {
  const bool tx_active = this->transmit_ && !this->dry_run_;
  // 19200 baud is about 1900 bytes/s, so a normal loop() pass sees a few dozen bytes at most.
  int budget = 512;
  uint8_t b;
  while (budget-- > 0 && this->available() > 0) {
    if (!this->read_byte(&b))
      break;
    if (this->parser_.feed(b)) {
      const SpaFrame &f = this->parser_.frame();
      ESP_LOGVV(TAG, "frame type=%02x len=%u", f.type, (unsigned) f.len);
      SpaUpdate u;
      this->decoder_.process(f, u);
      this->publish_(u);
      // A status frame just ended and nothing newer has arrived: this is the reply slot the keypad uses.
      if (tx_active && f.type == 0x30 && this->seq_.busy() && this->available() == 0)
        this->try_slot_(millis());
    }
  }

  if (tx_active) {
    const uint32_t tnow = millis();
    this->seq_.tick(tnow, this->decoder_.state());
    this->report_result_();
    if (this->setpoint_.active()) {
      SpaKey k = SpaKey::UP;
      if (this->setpoint_.tick(tnow, this->decoder_.state(), this->seq_.nav_done(), this->seq_.nav_failed(), &k)) {
        if (!this->seq_.request_nav(k, this->decoder_.state()))
          this->setpoint_.key_refused(this->seq_.error(), tnow);
      }
    }
    this->report_setpoint_result_();
    // While a key action is running, run the loop as fast as possible so the reply slot is not missed.
    if (this->seq_.busy())
      this->high_freq_.start();
    else
      this->high_freq_.stop();
  }

  uint32_t now = millis();
  if (now - this->last_stats_ms_ >= 60000) {
    this->last_stats_ms_ = now;
    ESP_LOGD(TAG, "frames ok=%u bad=%u resync=%u overflow=%u tx_frames=%u late_slots=%u",
             (unsigned) this->parser_.frames_ok(), (unsigned) this->parser_.frames_bad(),
             (unsigned) this->parser_.resyncs(), (unsigned) this->parser_.overflows(),
             (unsigned) this->seq_.frames_sent(), (unsigned) this->seq_.late_skips());
  }
  this->last_pass_end_us_ = micros();
}

void SpaPowerComponent::try_slot_(uint32_t now_ms) {
  // The status frame ended no earlier than the end of the previous loop pass, so this is the longest it can have
  // been since the frame ended.
  const uint32_t latency_us = micros() - this->last_pass_end_us_;
  uint8_t buf[16];
  size_t len = 0;
  if (!this->seq_.on_slot(now_ms, latency_us, this->decoder_.state(), buf, sizeof(buf), &len))
    return;
  this->write_array(buf, len);
  this->flush();
  ESP_LOGD(TAG, "sent %s (slot latency <= %u us)", hex_string(buf, len).c_str(), (unsigned) latency_us);
}

void SpaPowerComponent::report_setpoint_result_() {
  std::string msg;
  if (!this->setpoint_.take_result(&msg))
    return;
  if (this->setpoint_.last_ok())
    ESP_LOGI(TAG, "setpoint: %s", msg.c_str());
  else
    ESP_LOGW(TAG, "setpoint change failed: %s", msg.c_str());
}

void SpaPowerComponent::report_result_() {
  std::string msg;
  if (!this->seq_.take_result(&msg))
    return;
  if (this->seq_.last_ok())
    ESP_LOGI(TAG, "key action: %s", msg.c_str());
  else
    ESP_LOGW(TAG, "key action failed: %s", msg.c_str());
}

bool SpaPowerComponent::check_tx_(const char *what) {
  if (!this->transmit_) {
    ESP_LOGW(TAG, "%s: transmit is not enabled (set transmit: true)", what);
    return false;
  }
  return true;
}

void SpaPowerComponent::log_plan_(const char *what, SpaKey key, int presses) {
  uint8_t b0 = 0, b1 = 0, down[16], up[16];
  spa_key_bytes(key, &b0, &b1);
  size_t nd = spa_encode_key(b0, b1, 0x01, down, sizeof(down));
  size_t nu = spa_encode_key(0, 0, 0, up, sizeof(up));
  ESP_LOGI(TAG, "DRY RUN %s: %d press(es); each sends key-down %s, then %u ms later release %s", what, presses,
           hex_string(down, nd).c_str(), (unsigned) this->key_hold_ms_, hex_string(up, nu).c_str());
}

bool SpaPowerComponent::press_key(SpaKey key) {
  if (!this->check_tx_("key press"))
    return false;
  if (SpaTxSequencer::is_nav(key) && this->setpoint_.active()) {
    ESP_LOGW(TAG, "key press refused: a setpoint change is running");
    return false;
  }
  const SpaState &st = this->decoder_.state();
  if (this->dry_run_) {
    this->log_plan_("key press", key, 1);
    return true;
  }
  bool ok = SpaTxSequencer::is_nav(key) ? this->seq_.request_nav(key, st) : this->seq_.press_raw(key, st, millis());
  if (!ok)
    ESP_LOGW(TAG, "key press refused: %s", this->seq_.error());
  return ok;
}

bool SpaPowerComponent::request_pump1(uint8_t target) {
  if (!this->check_tx_("pump"))
    return false;
  const SpaState &st = this->decoder_.state();
  if (this->dry_run_) {
    if (!st.known) {
      ESP_LOGW(TAG, "DRY RUN pump: no status frame seen yet");
      return false;
    }
    this->log_plan_("pump", SpaKey::PUMP1, SpaTxSequencer::pump1_presses(st.pump1, target));
    return true;
  }
  bool ok = this->seq_.request_pump1(target, st, millis());
  if (!ok)
    ESP_LOGW(TAG, "pump request refused: %s", this->seq_.error());
  return ok;
}

bool SpaPowerComponent::request_toggle(SpaKey key, bool on) {
  const char *name = key == SpaKey::PUMP2 ? "pump 2" : "aux";
  if (!this->check_tx_(name))
    return false;
  const SpaState &st = this->decoder_.state();
  if (this->dry_run_) {
    if (!st.known) {
      ESP_LOGW(TAG, "DRY RUN %s: no status frame seen yet", name);
      return false;
    }
    bool cur = key == SpaKey::PUMP2 ? st.pump2 : st.aux;
    this->log_plan_(name, key, cur == on ? 0 : 1);
    return true;
  }
  bool ok = this->seq_.request_toggle(key, on, st, millis());
  if (!ok)
    ESP_LOGW(TAG, "%s request refused: %s", name, this->seq_.error());
  return ok;
}

bool SpaPowerComponent::request_setpoint(float target, float min_v, float max_v) {
  if (!this->check_tx_("setpoint"))
    return false;
  const SpaState &st = this->decoder_.state();
  if (this->dry_run_) {
    if (!st.known || !st.set_known) {
      ESP_LOGW(TAG, "DRY RUN setpoint: no status or setpoint read yet");
      return false;
    }
    float t = roundf(target / SpaSetpointController::STEP) * SpaSetpointController::STEP;
    int steps = (int) (fabsf(t - st.set_value) / SpaSetpointController::STEP + 0.5f);
    ESP_LOGI(TAG, "DRY RUN setpoint %.2f -> %.2f: select, up, select, then %d x %s, select (nothing sent)", st.set_value,
             t, steps, t >= st.set_value ? "up" : "down");
    return true;
  }
  if (this->seq_.busy()) {
    ESP_LOGW(TAG, "setpoint request refused: a key action is still running");
    return false;
  }
  bool ok = this->setpoint_.request(target, min_v, max_v, st, millis());
  if (!ok)
    ESP_LOGW(TAG, "setpoint request refused: %s", this->setpoint_.error());
  return ok;
}

void SpaPowerComponent::publish_(const SpaUpdate &u) {
  for (const auto &n : u.notes)
    ESP_LOGI(TAG, "%s", n.c_str());
  if (u.has_current_temp && this->current_temperature_ != nullptr)
    this->current_temperature_->publish_state(u.current_temp);
  if (u.has_set_temp && this->set_temperature_ != nullptr)
    this->set_temperature_->publish_state(u.set_temp);
  if (u.has_set_temp && this->setpoint_number_ != nullptr)
    this->setpoint_number_->publish_state(u.set_temp);
  if (u.has_light && this->light_level_ != nullptr)
    this->light_level_->publish_state(u.light_percent);
  if (u.has_line1 && this->line1_ != nullptr)
    this->line1_->publish_state(u.line1);
  if (u.has_line2 && this->line2_ != nullptr)
    this->line2_->publish_state(u.line2);
  if (u.has_key && this->key_ != nullptr)
    this->key_->publish_state(u.key);
  if (u.has_pump) {
    if (this->pump_state_ != nullptr)
      this->pump_state_->publish_state(u.pump);
    if (this->pump_select_ != nullptr)
      this->pump_select_->publish_state(u.pump);
  }
  if (u.has_status_raw && this->status_raw_ != nullptr)
    this->status_raw_->publish_state(u.status_raw);
  if (u.has_digits && this->big_digits_ != nullptr)
    this->big_digits_->publish_state(u.digits);
  if (u.has_light_color && this->light_color_ != nullptr)
    this->light_color_->publish_state(u.light_color);
  if (u.has_pump_running && this->pump_running_ != nullptr)
    this->pump_running_->publish_state(u.pump_running);
  if (u.has_heater && this->heater_ != nullptr)
    this->heater_->publish_state(u.heater);
  if (u.has_ozone && this->ozone_ != nullptr)
    this->ozone_->publish_state(u.ozone);
  if (u.has_pump2_running) {
    if (this->pump2_running_ != nullptr)
      this->pump2_running_->publish_state(u.pump2_running);
    if (this->pump2_switch_ != nullptr)
      this->pump2_switch_->publish_state(u.pump2_running);
  }
  if (u.has_aux) {
    if (this->aux_ != nullptr)
      this->aux_->publish_state(u.aux);
    if (this->aux_switch_ != nullptr)
      this->aux_switch_->publish_state(u.aux);
  }
}

void SpaPowerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Spa Power keypad bus:");
  ESP_LOGCONFIG(TAG, "  Transmit: %s%s", this->transmit_ ? "enabled" : "disabled",
                this->transmit_ && this->dry_run_ ? " (DRY RUN, nothing is sent)" : "");
  if (this->transmit_) {
    ESP_LOGCONFIG(TAG, "  Key hold: %u ms, spacing: %u ms, response timeout: %u ms, max reply latency: %u us",
                  (unsigned) this->key_hold_ms_, (unsigned) this->key_spacing_ms_,
                  (unsigned) this->response_timeout_ms_, (unsigned) this->max_reply_latency_us_);
  }
  LOG_SENSOR("  ", "Current temperature", this->current_temperature_);
  LOG_SENSOR("  ", "Set temperature", this->set_temperature_);
  LOG_SENSOR("  ", "Light level", this->light_level_);
  LOG_TEXT_SENSOR("  ", "Line 1", this->line1_);
  LOG_TEXT_SENSOR("  ", "Line 2", this->line2_);
  LOG_TEXT_SENSOR("  ", "Key", this->key_);
  LOG_TEXT_SENSOR("  ", "Pump state", this->pump_state_);
  LOG_TEXT_SENSOR("  ", "Light colour (experimental)", this->light_color_);
  LOG_TEXT_SENSOR("  ", "Big digits (diagnostic)", this->big_digits_);
  LOG_TEXT_SENSOR("  ", "Status raw", this->status_raw_);
  LOG_BINARY_SENSOR("  ", "Pump running", this->pump_running_);
  LOG_BINARY_SENSOR("  ", "Heater", this->heater_);
  LOG_BINARY_SENSOR("  ", "Ozone", this->ozone_);
  LOG_BINARY_SENSOR("  ", "Pump 2 running", this->pump2_running_);
  LOG_BINARY_SENSOR("  ", "Aux", this->aux_);
  this->check_uart_settings(19200, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

void SpaPowerKeyButton::press_action() { this->parent_->press_key(static_cast<SpaKey>(this->key_)); }

void SpaPowerSetpointNumber::control(float value) {
  this->parent_->request_setpoint(value, this->traits.get_min_value(), this->traits.get_max_value());
}

void SpaPowerSwitch::write_state(bool state) {
  // The displayed state follows what the controller reports, so it is not published here.
  this->parent_->request_toggle(static_cast<SpaKey>(this->key_), state);
}

void SpaPowerPumpSelect::control(const std::string &value) {
  uint8_t target = value == "off" ? 0 : (value == "auto" ? 1 : 2);
  this->parent_->request_pump1(target);
}

}  // namespace spa_power
}  // namespace esphome
