#include "gdo_cover.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gdo {

static const char *const TAG = "gdo.cover";

const float UNKNOWN_POSITION = 0.5f;

using namespace esphome::cover;

void GdoCover::dump_config() {
  LOG_COVER("", "Time Based Endstop Cover", this);
  LOG_BINARY_SENSOR("  ", "Open Endstop", this->open_endstop_);
  ESP_LOGCONFIG(TAG, "  Open Duration: %.1fs", this->open_duration_ / 1e3f);
  LOG_BINARY_SENSOR("  ", "Close Endstop", this->close_endstop_);
  ESP_LOGCONFIG(TAG, "  Close Duration: %.1fs", this->close_duration_ / 1e3f);
}

void GdoCover::setup() {
  // Restore state after restart
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->position = UNKNOWN_POSITION;
  }
  if (this->open_endstop_ != nullptr && this->open_endstop_->has_state()) {
    // Fix restored state based on open endstop
    if (this->open_endstop_->state) {
      this->position = COVER_OPEN;
    } else if (this->position == COVER_OPEN) {
      this->position = UNKNOWN_POSITION;
    }
  }
  if (this->close_endstop_ != nullptr && this->close_endstop_->has_state()) {
    // Fix restored state based on closed endstop
    if (this->close_endstop_->state) {
      this->position = COVER_CLOSED;
    } else if (this->position == COVER_CLOSED) {
      this->position = UNKNOWN_POSITION;
    }
  }
  if (this->open_endstop_ != nullptr) {
    this->open_endstop_->add_on_state_callback([this](bool value) {
      if (value) {
        // Reached the open endstop. Update state
        float dur = (millis() - this->start_dir_time_) / 1e3f;
        ESP_LOGI(TAG, "Open endstop reached. Took %.1fs.", dur);
        this->position = COVER_OPEN;
        this->target_position_ = COVER_OPEN;
        if (this->current_operation != COVER_OPERATION_IDLE) {
          this->last_direction_before_idle_ = this->current_operation;
        }
        this->current_operation = COVER_OPERATION_IDLE;
        this->pending_operation_ = COVER_OPERATION_IDLE;
        this->pending_operation_time_ = 0;
        this->publish_state();
      } else {
        // Moved away from the open endstop.
        ESP_LOGI(TAG, "Open endstop released.");
        if (this->current_operation == COVER_OPERATION_IDLE) {
          if (this->pending_operation_ == COVER_OPERATION_CLOSING) {
            // Our command has been confirmed by sensor, activate the pending operation
            ESP_LOGI(TAG, "Sensor confirmed door movement, activating pending CLOSING operation");
            this->current_operation = COVER_OPERATION_CLOSING;
            this->pending_operation_ = COVER_OPERATION_IDLE;
            this->pending_operation_time_ = 0;
            this->action_delay_end_time_ = 0;
            const uint32_t now = millis();
            this->start_dir_time_ = now;
            this->last_recompute_time_ = now;
            this->publish_state();
          } else {
            // External control - assume target position is fully closed
            // and start updating state without triggering a press.
            this->target_position_ = COVER_CLOSED;
            this->start_direction_(COVER_OPERATION_CLOSING, false);
          }
        }
      }
    });
  }
  if (this->close_endstop_ != nullptr) {
    this->close_endstop_->add_on_state_callback([this](bool value) {
      if (value) {
        // Reached the closed endstop. Update state
        float dur = (millis() - this->start_dir_time_) / 1e3f;
        ESP_LOGI(TAG, "Closed endstop reached. Took %.1fs.", dur);
        this->position = COVER_CLOSED;
        this->target_position_ = COVER_CLOSED;
        if (this->current_operation != COVER_OPERATION_IDLE) {
          this->last_direction_before_idle_ = this->current_operation;
        }
        this->current_operation = COVER_OPERATION_IDLE;
        this->pending_operation_ = COVER_OPERATION_IDLE;
        this->pending_operation_time_ = 0;
        this->publish_state();
      } else {
        // Moved away from the closed endstop.
        ESP_LOGI(TAG, "Closed endstop released.");
        if (this->current_operation == COVER_OPERATION_IDLE) {
          if (this->pending_operation_ == COVER_OPERATION_OPENING) {
            // Our command has been confirmed by sensor, activate the pending operation
            ESP_LOGI(TAG, "Sensor confirmed door movement, activating pending OPENING operation");
            this->current_operation = COVER_OPERATION_OPENING;
            this->pending_operation_ = COVER_OPERATION_IDLE;
            this->pending_operation_time_ = 0;
            this->action_delay_end_time_ = 0;
            const uint32_t now = millis();
            this->start_dir_time_ = now;
            this->last_recompute_time_ = now;
            this->publish_state();
          } else {
            // External control - assume target position is fully open
            // and start updating state without triggering a press.
            this->target_position_ = COVER_OPEN;
            this->start_direction_(COVER_OPERATION_OPENING, false);
          }
        }
      }
    });
  }
}

void GdoCover::loop() {
  const uint32_t now = millis();

  // Correct position if it's inconsistent with sensor state (while idle)
  if (this->current_operation == COVER_OPERATION_IDLE && this->pending_operation_ == COVER_OPERATION_IDLE) {
    if (this->close_endstop_ != nullptr && this->close_endstop_->state && this->position != COVER_CLOSED) {
      ESP_LOGI(TAG, "Correcting position: close_endstop is active but position was %.0f%%. Setting to CLOSED.",
               this->position * 100);
      this->position = COVER_CLOSED;
      this->publish_state();
    } else if (this->open_endstop_ != nullptr && this->open_endstop_->state && this->position != COVER_OPEN) {
      ESP_LOGI(TAG, "Correcting position: open_endstop is active but position was %.0f%%. Setting to OPEN.",
               this->position * 100);
      this->position = COVER_OPEN;
      this->publish_state();
    }
  }

  // Check if action delay has completed
  if (this->pending_operation_ != COVER_OPERATION_IDLE && this->action_delay_end_time_ > 0 && now >= this->action_delay_end_time_) {
    ESP_LOGI(TAG, "Action delay completed, activating %s operation",
             this->pending_operation_ == COVER_OPERATION_OPENING ? "OPENING" : "CLOSING");
    this->current_operation = this->pending_operation_;
    this->pending_operation_ = COVER_OPERATION_IDLE;
    this->action_delay_end_time_ = 0;
    // Set timing from when door ACTUALLY starts moving
    this->start_dir_time_ = now;
    this->last_recompute_time_ = now;
    this->publish_state();
  }

  // Check for pending operation timeout (door never started moving)
  if (this->pending_operation_ != COVER_OPERATION_IDLE && this->pending_operation_time_ > 0) {
    const uint32_t pending_duration = now - this->pending_operation_time_;
    if (pending_duration > 3000) {  // 3 second timeout
      ESP_LOGW(TAG, "Pending operation timed out after %ums. Door did not start moving.", pending_duration);
      this->pending_operation_ = COVER_OPERATION_IDLE;
      this->pending_operation_time_ = 0;
      this->action_delay_end_time_ = 0;

      // Trust sensor state if available
      if (this->close_endstop_ != nullptr && this->close_endstop_->state) {
        ESP_LOGI(TAG, "close_endstop shows door is closed. Setting position to CLOSED.");
        this->position = COVER_CLOSED;
      } else if (this->open_endstop_ != nullptr && this->open_endstop_->state) {
        ESP_LOGI(TAG, "open_endstop shows door is open. Setting position to OPEN.");
        this->position = COVER_OPEN;
      } else {
        ESP_LOGI(TAG, "No sensor confirmation. Setting position to UNKNOWN.");
        this->position = UNKNOWN_POSITION;
      }

      this->publish_state();
    }
  }

  if (this->current_operation == COVER_OPERATION_IDLE) {
    return;
  }

  // Recompute position every loop cycle
  this->recompute_position_();

  if (this->is_at_target_()) {
    if (this->target_position_ == COVER_OPEN || this->target_position_ == COVER_CLOSED) {
      // Don't trigger stop, let the cover stop by itself.
      if (this->current_operation != COVER_OPERATION_IDLE) {
        this->last_direction_before_idle_ = this->current_operation;
      }
      this->current_operation = COVER_OPERATION_IDLE;
      this->pending_operation_ = COVER_OPERATION_IDLE;
      this->pending_operation_time_ = 0;
    } else {
      this->start_direction_(COVER_OPERATION_IDLE);
    }
    this->publish_state();
  } else if ((this->current_operation == COVER_OPERATION_OPENING && this->open_endstop_ != nullptr &&
              now - this->start_dir_time_ > this->open_duration_) ||
             (this->current_operation == COVER_OPERATION_CLOSING && this->close_endstop_ != nullptr &&
              now - this->start_dir_time_ > this->close_duration_)) {
    ESP_LOGI(TAG, "Failed to reach endstop. Likely stopped externally.");

    // Check sensor state before setting position to UNKNOWN
    if (this->close_endstop_ != nullptr && this->close_endstop_->state) {
      ESP_LOGI(TAG, "close_endstop shows door is closed. Setting position to CLOSED.");
      this->position = COVER_CLOSED;
    } else if (this->open_endstop_ != nullptr && this->open_endstop_->state) {
      ESP_LOGI(TAG, "open_endstop shows door is open. Setting position to OPEN.");
      this->position = COVER_OPEN;
    } else {
      ESP_LOGI(TAG, "No sensor confirmation. Setting position to UNKNOWN.");
      this->position = UNKNOWN_POSITION;
    }

    if (this->current_operation != COVER_OPERATION_IDLE) {
      this->last_direction_before_idle_ = this->current_operation;
    }
    this->current_operation = COVER_OPERATION_IDLE;
    this->pending_operation_ = COVER_OPERATION_IDLE;
    this->pending_operation_time_ = 0;
    this->publish_state();
  }

  // Send current position every second
  if (now - this->last_publish_time_ > 1000) {
    this->publish_state(false);
    this->last_publish_time_ = now;
  }
}

float GdoCover::get_setup_priority() const { return setup_priority::DATA; }

CoverTraits GdoCover::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(true);
  return traits;
}

void GdoCover::control(const CoverCall &call) {
  if (call.get_stop()) {
    this->start_direction_(COVER_OPERATION_IDLE);
    this->publish_state();
  }
  if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    if (pos == this->position) {
      ESP_LOGI(TAG, "Nothing to do. Already at target position.");
    } else {
      auto op = pos < this->position ? COVER_OPERATION_CLOSING : COVER_OPERATION_OPENING;
      this->target_position_ = pos;
      this->start_direction_(op);
      this->publish_state();
    }
  }
}

void GdoCover::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

bool GdoCover::is_at_target_() const {
  // If we have a pending operation that differs from current, we're NOT at target
  // (we're waiting to change direction)
  if (this->pending_operation_ != COVER_OPERATION_IDLE && this->pending_operation_ != this->current_operation) {
    return false;
  }

  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      if (this->target_position_ == COVER_OPEN && this->open_endstop_ != nullptr) {
        return this->open_endstop_->state;
      }
      return this->position >= this->target_position_;
    case COVER_OPERATION_CLOSING:
      if (this->target_position_ == COVER_CLOSED && this->close_endstop_ != nullptr) {
        return this->close_endstop_->state;
      }
      return this->position <= this->target_position_;
    case COVER_OPERATION_IDLE:
    default:
      return true;
  }
}

void GdoCover::start_direction_(CoverOperation dir, bool perform_trigger) {
  if (dir == this->current_operation) {
    ESP_LOGI(TAG, "Nothing to do. CoverOperation %d didn't change.", dir);
    return;
  }

  this->recompute_position_();
  Trigger<> *trig = nullptr;

  // Determine how many pulses are needed based on OSD terminal state machine
  switch (dir) {
    case COVER_OPERATION_IDLE:
      // Stopping while moving: 1 pulse
      if (this->current_operation == COVER_OPERATION_OPENING) {
        ESP_LOGI(TAG, "Door is opening. Asked to stop.");
        trig = this->single_press_trigger_;
      } else if (this->current_operation == COVER_OPERATION_CLOSING) {
        ESP_LOGI(TAG, "Door is closing. Asked to stop.");
        trig = this->single_press_trigger_;
      } else {
        return;
      }
      break;

    case COVER_OPERATION_OPENING:
      if (this->current_operation == COVER_OPERATION_IDLE) {
        // From idle state
        if (this->position == COVER_CLOSED) {
          ESP_LOGI(TAG, "Door is fully closed. Asked to open.");
          trig = this->single_press_trigger_;
        } else if (this->position == COVER_OPEN) {
          ESP_LOGW(TAG, "Door is fully open. Cannot open more.");
          return;
        } else {
          // Partially open - depends on previous direction
          if (this->last_direction_before_idle_ == COVER_OPERATION_CLOSING) {
            ESP_LOGI(TAG, "Door stopped while closing. Asked to open (opposite direction).");
            trig = this->single_press_trigger_;
          } else if (this->last_direction_before_idle_ == COVER_OPERATION_OPENING) {
            ESP_LOGI(TAG, "Door stopped while opening. Asked to resume opening (same direction).");
            trig = this->triple_press_trigger_;
          } else {
            // Unknown previous direction, assume opposite
            ESP_LOGI(TAG, "Door is partially open (unknown direction). Asked to open.");
            trig = this->single_press_trigger_;
          }
        }
      } else if (this->current_operation == COVER_OPERATION_CLOSING) {
        ESP_LOGI(TAG, "Door is closing. Asked to reverse and open.");
        trig = this->double_press_trigger_;
      } else {
        return;
      }
      break;

    case COVER_OPERATION_CLOSING:
      if (this->current_operation == COVER_OPERATION_IDLE) {
        // From idle state
        if (this->position == COVER_CLOSED) {
          ESP_LOGI(TAG, "Door is fully closed. Cannot close more.");
          return;
        } else if (this->position == COVER_OPEN) {
          ESP_LOGI(TAG, "Door is fully open. Asked to close.");
          trig = this->single_press_trigger_;
        } else {
          // Partially open - depends on previous direction
          if (this->last_direction_before_idle_ == COVER_OPERATION_OPENING) {
            ESP_LOGI(TAG, "Door stopped while opening. Asked to close (opposite direction).");
            trig = this->single_press_trigger_;
          } else if (this->last_direction_before_idle_ == COVER_OPERATION_CLOSING) {
            ESP_LOGI(TAG, "Door stopped while closing. Asked to resume closing (same direction).");
            trig = this->triple_press_trigger_;
          } else {
            // Unknown previous direction, assume opposite
            ESP_LOGI(TAG, "Door is partially open (unknown direction). Asked to close.");
            trig = this->single_press_trigger_;
          }
        }
      } else if (this->current_operation == COVER_OPERATION_OPENING) {
        ESP_LOGI(TAG, "Door is opening. Asked to reverse and close.");
        trig = this->double_press_trigger_;
      } else {
        return;
      }
      break;

    default:
      return;
  }

  // Save previous direction before updating current operation
  if (this->current_operation != COVER_OPERATION_IDLE) {
    this->last_direction_before_idle_ = this->current_operation;
  }

  // Check if we should defer state change until sensor confirms movement
  bool defer_state_change = false;
  if (dir == COVER_OPERATION_OPENING && this->close_endstop_ != nullptr) {
    ESP_LOGD(TAG, "Checking close_endstop state: %s", this->close_endstop_->state ? "true (closed)" : "false (open)");
    if (this->close_endstop_->state) {
      // Door is at closed endstop, wait for sensor to confirm it moved
      ESP_LOGI(TAG, "Waiting for close_endstop to confirm door movement");
      defer_state_change = true;
    }
  } else if (dir == COVER_OPERATION_CLOSING && this->open_endstop_ != nullptr) {
    ESP_LOGD(TAG, "Checking open_endstop state: %s", this->open_endstop_->state ? "true (open)" : "false (closed)");
    if (this->open_endstop_->state) {
      // Door is at open endstop, wait for sensor to confirm it moved
      ESP_LOGI(TAG, "Waiting for open_endstop to confirm door movement");
      defer_state_change = true;
    }
  }

  const uint32_t now = millis();

  // Calculate action delay based on which trigger we're using
  // BUT: STOP commands (IDLE) should take effect immediately
  uint32_t action_delay = 0;
  if (perform_trigger && trig != nullptr && dir != COVER_OPERATION_IDLE) {
    if (trig == this->single_press_trigger_) {
      action_delay = this->relay_on_duration_;
    } else if (trig == this->double_press_trigger_) {
      action_delay = (2 * this->relay_on_duration_) + this->pulse_delay_;
    } else if (trig == this->triple_press_trigger_) {
      action_delay = (3 * this->relay_on_duration_) + (2 * this->pulse_delay_);
    }
    ESP_LOGD(TAG, "Action delay calculated: %ums", action_delay);
  }

  if (defer_state_change) {
    // Waiting for sensor confirmation - do NOT use action delay timer
    // Sensor callback will activate operation when confirmed
    this->pending_operation_ = dir;
    this->pending_operation_time_ = now;
    this->action_delay_end_time_ = 0;  // Sensor callback handles activation
    ESP_LOGI(TAG, "Deferred state change (sensor confirmation). current_operation stays %d, pending_operation set to %d",
             this->current_operation, dir);
  } else if (action_delay > 0 && dir != COVER_OPERATION_IDLE) {
    // Waiting for action delay to complete (no sensor to wait for)
    this->pending_operation_ = dir;
    this->pending_operation_time_ = now;
    this->action_delay_end_time_ = now + action_delay;
    ESP_LOGI(TAG, "Deferring state change for %ums action delay. pending_operation set to %d", action_delay, dir);
  } else {
    // Immediate state change (no sensor wait, no action delay)
    this->current_operation = dir;
    this->pending_operation_ = COVER_OPERATION_IDLE;
    this->pending_operation_time_ = 0;
    this->action_delay_end_time_ = 0;
    this->start_dir_time_ = now;
    this->last_recompute_time_ = now;
    ESP_LOGD(TAG, "Immediate state change. current_operation set to %d", dir);
  }

  if (perform_trigger && trig != nullptr) {
    this->stop_prev_trigger_();
    trig->trigger();
    this->prev_command_trigger_ = trig;
  }
}

void GdoCover::recompute_position_() {
  float dir;
  float action_dur;
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      dir = 1.0f;
      action_dur = this->open_duration_;
      break;
    case COVER_OPERATION_CLOSING:
      dir = -1.0f;
      action_dur = this->close_duration_;
      break;
    case COVER_OPERATION_IDLE:
    default:
      return;
  }
  const uint32_t now = millis();
  this->position += dir * (now - this->last_recompute_time_) / action_dur;
  this->position = clamp(this->position, 0.0f, 1.0f);
  this->last_recompute_time_ = now;
}

}  // namespace gdo
}  // namespace esphome
