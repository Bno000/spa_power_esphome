#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
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

 protected:
  void publish_(const SpaUpdate &u);

  SpaFrameParser parser_;
  SpaDecoder decoder_;
  uint32_t last_stats_ms_{0};

  sensor::Sensor *current_temperature_{nullptr};
  sensor::Sensor *set_temperature_{nullptr};
  sensor::Sensor *light_level_{nullptr};
  text_sensor::TextSensor *line1_{nullptr};
  text_sensor::TextSensor *line2_{nullptr};
  text_sensor::TextSensor *key_{nullptr};
  text_sensor::TextSensor *pump_state_{nullptr};
  text_sensor::TextSensor *status_raw_{nullptr};
};

}  // namespace spa_power
}  // namespace esphome
