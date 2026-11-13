#include "spa_power.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace spa_power {

static const char *const TAG = "spa_power";

void SpaPowerComponent::setup() { this->last_stats_ms_ = millis(); }

void SpaPowerComponent::loop() {
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
    }
  }

  uint32_t now = millis();
  if (now - this->last_stats_ms_ >= 60000) {
    this->last_stats_ms_ = now;
    ESP_LOGD(TAG, "frames ok=%u bad=%u resync=%u overflow=%u", (unsigned) this->parser_.frames_ok(),
             (unsigned) this->parser_.frames_bad(), (unsigned) this->parser_.resyncs(),
             (unsigned) this->parser_.overflows());
  }
}

void SpaPowerComponent::publish_(const SpaUpdate &u) {
  for (const auto &n : u.notes)
    ESP_LOGI(TAG, "%s", n.c_str());
  if (u.has_current_temp && this->current_temperature_ != nullptr)
    this->current_temperature_->publish_state(u.current_temp);
  if (u.has_set_temp && this->set_temperature_ != nullptr)
    this->set_temperature_->publish_state(u.set_temp);
  if (u.has_light && this->light_level_ != nullptr)
    this->light_level_->publish_state(u.light_percent);
  if (u.has_line1 && this->line1_ != nullptr)
    this->line1_->publish_state(u.line1);
  if (u.has_line2 && this->line2_ != nullptr)
    this->line2_->publish_state(u.line2);
  if (u.has_key && this->key_ != nullptr)
    this->key_->publish_state(u.key);
  if (u.has_pump && this->pump_state_ != nullptr)
    this->pump_state_->publish_state(u.pump);
  if (u.has_status_raw && this->status_raw_ != nullptr)
    this->status_raw_->publish_state(u.status_raw);
  if (u.has_light_color && this->light_color_ != nullptr)
    this->light_color_->publish_state(u.light_color);
  if (u.has_pump_running && this->pump_running_ != nullptr)
    this->pump_running_->publish_state(u.pump_running);
  if (u.has_heater && this->heater_ != nullptr)
    this->heater_->publish_state(u.heater);
  if (u.has_ozone && this->ozone_ != nullptr)
    this->ozone_->publish_state(u.ozone);
  if (u.has_pump2_running && this->pump2_running_ != nullptr)
    this->pump2_running_->publish_state(u.pump2_running);
  if (u.has_aux && this->aux_ != nullptr)
    this->aux_->publish_state(u.aux);
}

void SpaPowerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Spa Power keypad bus (read only):");
  LOG_SENSOR("  ", "Current temperature", this->current_temperature_);
  LOG_SENSOR("  ", "Set temperature", this->set_temperature_);
  LOG_SENSOR("  ", "Light level", this->light_level_);
  LOG_TEXT_SENSOR("  ", "Line 1", this->line1_);
  LOG_TEXT_SENSOR("  ", "Line 2", this->line2_);
  LOG_TEXT_SENSOR("  ", "Key", this->key_);
  LOG_TEXT_SENSOR("  ", "Pump state", this->pump_state_);
  LOG_TEXT_SENSOR("  ", "Light colour (experimental)", this->light_color_);
  LOG_TEXT_SENSOR("  ", "Status raw", this->status_raw_);
  LOG_BINARY_SENSOR("  ", "Pump running", this->pump_running_);
  LOG_BINARY_SENSOR("  ", "Heater", this->heater_);
  LOG_BINARY_SENSOR("  ", "Ozone", this->ozone_);
  LOG_BINARY_SENSOR("  ", "Pump 2 running", this->pump2_running_);
  LOG_BINARY_SENSOR("  ", "Aux", this->aux_);
  this->check_uart_settings(19200, 1, uart::UART_CONFIG_PARITY_NONE, 8);
}

}  // namespace spa_power
}  // namespace esphome
