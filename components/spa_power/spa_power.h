#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "spa_power_parser.h"

namespace esphome {
namespace spa_power {

class SpaPowerComponent : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_current_temperature_sensor(sensor::Sensor *s) { this->current_temperature_ = s; }
  void set_set_temperature_sensor(sensor::Sensor *s) { this->set_temperature_ = s; }
  void set_light_level_sensor(sensor::Sensor *s) { this->light_level_ = s; }
  void set_line1_text_sensor(text_sensor::TextSensor *s) { this->line1_ = s; }
  void set_line2_text_sensor(text_sensor::TextSensor *s) { this->line2_ = s; }
  void set_key_text_sensor(text_sensor::TextSensor *s) { this->key_ = s; }
  void set_pump_state_text_sensor(text_sensor::TextSensor *s) { this->pump_state_ = s; }
  void set_status_raw_text_sensor(text_sensor::TextSensor *s) { this->status_raw_ = s; }
  void set_pump_running_binary_sensor(binary_sensor::BinarySensor *s) { this->pump_running_ = s; }
  void set_pump2_running_binary_sensor(binary_sensor::BinarySensor *s) { this->pump2_running_ = s; }
  void set_aux_binary_sensor(binary_sensor::BinarySensor *s) { this->aux_ = s; }
  void set_heater_binary_sensor(binary_sensor::BinarySensor *s) { this->heater_ = s; }
  void set_ozone_binary_sensor(binary_sensor::BinarySensor *s) { this->ozone_ = s; }
  void set_light_color_text_sensor(text_sensor::TextSensor *s) { this->light_color_ = s; }

  // Controls (only active with transmit: true; with dry_run: true they only log what they would send).
  void set_pump2_switch(switch_::Switch *s) { this->pump2_switch_ = s; }
  void set_aux_switch(switch_::Switch *s) { this->aux_switch_ = s; }
  void set_pump_select(select::Select *s) { this->pump_select_ = s; }

  void set_transmit(bool v) { this->transmit_ = v; }
  void set_dry_run(bool v) { this->dry_run_ = v; }
  void set_key_hold_ms(uint32_t v) { this->key_hold_ms_ = v; }
  void set_response_timeout_ms(uint32_t v) { this->response_timeout_ms_ = v; }
  void set_max_reply_latency_us(uint32_t v) { this->max_reply_latency_us_ = v; }

  bool press_key(SpaKey key);                  // one raw key press
  bool request_pump1(uint8_t target);          // 0 off, 1 auto, 2 on: presses the key until the pump reports it
  bool request_toggle(SpaKey key, bool on);    // pump 2 or aux

 protected:
  void publish_(const SpaUpdate &u);
  void try_slot_(uint32_t now_ms);
  void report_result_();
  bool check_tx_(const char *what);
  void log_plan_(const char *what, SpaKey key, int presses);

  SpaFrameParser parser_;
  SpaDecoder decoder_;
  SpaTxSequencer seq_;
  HighFrequencyLoopRequester high_freq_;
  uint32_t last_stats_ms_{0};
  uint32_t last_pass_end_us_{0};

  bool transmit_{false};
  bool dry_run_{true};
  uint32_t key_hold_ms_{150};
  uint32_t response_timeout_ms_{800};
  uint32_t max_reply_latency_us_{1500};

  sensor::Sensor *current_temperature_{nullptr};
  sensor::Sensor *set_temperature_{nullptr};
  sensor::Sensor *light_level_{nullptr};
  text_sensor::TextSensor *line1_{nullptr};
  text_sensor::TextSensor *line2_{nullptr};
  text_sensor::TextSensor *key_{nullptr};
  text_sensor::TextSensor *pump_state_{nullptr};
  text_sensor::TextSensor *status_raw_{nullptr};
  text_sensor::TextSensor *light_color_{nullptr};
  binary_sensor::BinarySensor *pump_running_{nullptr};
  binary_sensor::BinarySensor *heater_{nullptr};
  binary_sensor::BinarySensor *ozone_{nullptr};
  binary_sensor::BinarySensor *pump2_running_{nullptr};
  binary_sensor::BinarySensor *aux_{nullptr};
  switch_::Switch *pump2_switch_{nullptr};
  switch_::Switch *aux_switch_{nullptr};
  select::Select *pump_select_{nullptr};
};

// One key press per button press (pump 1, pump 2 or aux key), no target state.
class SpaPowerKeyButton : public button::Button, public Parented<SpaPowerComponent> {
 public:
  void set_key(uint8_t k) { this->key_ = k; }

 protected:
  void press_action() override;
  uint8_t key_{0};
};

// Pump 2 or aux as a switch. The state shown is what the controller reports, not what was commanded.
class SpaPowerSwitch : public switch_::Switch, public Parented<SpaPowerComponent> {
 public:
  void set_key(uint8_t k) { this->key_ = k; }

 protected:
  void write_state(bool state) override;
  uint8_t key_{1};
};

// Pump 1 mode: off / auto / on.
class SpaPowerPumpSelect : public select::Select, public Parented<SpaPowerComponent> {
 protected:
  void control(const std::string &value) override;
};

}  // namespace spa_power
}  // namespace esphome
