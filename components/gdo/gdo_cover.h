#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/cover/cover.h"

namespace esphome {
namespace gdo {

class GdoCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  Trigger<> *get_single_press_trigger() const { return this->single_press_trigger_; }
  Trigger<> *get_double_press_trigger() const { return this->double_press_trigger_; }
  Trigger<> *get_triple_press_trigger() const { return this->triple_press_trigger_; }
  void set_open_endstop(binary_sensor::BinarySensor *open_endstop) { this->open_endstop_ = open_endstop; }
  void set_close_endstop(binary_sensor::BinarySensor *close_endstop) { this->close_endstop_ = close_endstop; }
  void set_open_duration(uint32_t open_duration) { this->open_duration_ = open_duration; }
  void set_close_duration(uint32_t close_duration) { this->close_duration_ = close_duration; }
  void set_single_press_duration(uint32_t duration) { this->single_press_duration_ = duration; }
  void set_double_press_duration(uint32_t duration) { this->double_press_duration_ = duration; }
  void set_triple_press_duration(uint32_t duration) { this->triple_press_duration_ = duration; }

  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void stop_prev_trigger_();
  bool is_at_target_() const;

  void start_direction_(cover::CoverOperation dir, bool perform_trigger = true);

  void recompute_position_();

  binary_sensor::BinarySensor *open_endstop_{nullptr};
  binary_sensor::BinarySensor *close_endstop_{nullptr};
  uint32_t open_duration_;
  uint32_t close_duration_;
  uint32_t single_press_duration_{100};
  uint32_t double_press_duration_{1200};
  uint32_t triple_press_duration_{2300};
  Trigger<> *single_press_trigger_{new Trigger<>()};
  Trigger<> *double_press_trigger_{new Trigger<>()};
  Trigger<> *triple_press_trigger_{new Trigger<>()};
  Trigger<> *prev_command_trigger_{nullptr};
  uint32_t last_recompute_time_{0};
  uint32_t start_dir_time_{0};
  uint32_t last_publish_time_{0};
  float target_position_{0};
  cover::CoverOperation last_direction_before_idle_{cover::COVER_OPERATION_IDLE};
  cover::CoverOperation pending_operation_{cover::COVER_OPERATION_IDLE};
  uint32_t pending_operation_time_{0};
};

}  // namespace gdo
}  // namespace esphome
