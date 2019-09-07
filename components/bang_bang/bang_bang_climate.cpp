#include "bang_bang_climate.h"
#include "esphome/core/log.h"

namespace esphome {
namespace bang_bang {

static const char *TAG = "bang_bang.climate";

void BangBangClimate::setup() {
  this->sensor_->add_on_state_callback([this](float state) {
    this->current_temperature = state;
    // control may have changed, recompute
    this->compute_state_();
    // current temperature changed, publish state
    this->publish_state();
  });
  this->current_temperature = this->sensor_->state;
  // restore set points
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->to_call(this).perform();
  } else {
    // restore from defaults, change_away handles those for us
    this->mode = climate::CLIMATE_MODE_AUTO;
    this->change_away_(false);
  }
}
void BangBangClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (call.get_target_temperature_low().has_value())
    this->target_temperature_low = *call.get_target_temperature_low();
  if (call.get_target_temperature_high().has_value())
    this->target_temperature_high = *call.get_target_temperature_high();
  if (call.get_away().has_value())
    this->change_away_(*call.get_away());

  this->compute_state_();
  this->publish_state();
}
climate::ClimateTraits BangBangClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supports_current_temperature(true);
  traits.set_supports_auto_mode(true);
  traits.set_supports_cool_mode(this->supports_cool_);
  traits.set_supports_heat_mode(this->supports_heat_);
  traits.set_supports_two_point_target_temperature(true);
  traits.set_supports_away(this->supports_away_);
  return traits;
}
void BangBangClimate::compute_state_() {
  if (this->mode != climate::CLIMATE_MODE_AUTO) {
    // in non-auto mode
    this->switch_to_mode_(this->mode);
    return;
  }

  // auto mode, compute target mode
  if (isnan(this->current_temperature) || isnan(this->target_temperature_low) || isnan(this->target_temperature_high)) {
    // if any control values are nan, go to OFF (idle) mode
    this->switch_to_mode_(climate::CLIMATE_MODE_OFF);
    return;
  }
  const bool too_cold = this->current_temperature < this->target_temperature_low;
  const bool too_hot = this->current_temperature > this->target_temperature_high;

  climate::ClimateMode target_mode;
  if (too_cold) {
    // too cold -> enable heating if possible, else idle
    if (this->supports_heat_)
      target_mode = climate::CLIMATE_MODE_HEAT;
    else
      target_mode = climate::CLIMATE_MODE_OFF;
  } else if (too_hot) {
    // too hot -> enable cooling if possible, else idle
    if (this->supports_cool_)
      target_mode = climate::CLIMATE_MODE_COOL;
    else
      target_mode = climate::CLIMATE_MODE_OFF;
  } else {
    // neither too hot nor too cold -> in range
    if (this->supports_cool_ && this->supports_heat_) {
      // if supports both ends, go to idle mode
      target_mode = climate::CLIMATE_MODE_OFF;
    } else {
      // else use current mode and don't change (hysteresis)
      target_mode = this->internal_mode_;
    }
  }

  this->switch_to_mode_(target_mode);
}
void BangBangClimate::switch_to_mode_(climate::ClimateMode mode) {
  if (mode == this->internal_mode_)
    // already in target mode
    return;

  if (this->prev_trigger_ != nullptr) {
    this->prev_trigger_->stop();
    this->prev_trigger_ = nullptr;
  }
  Trigger<> *trig;
  switch (mode) {
    case climate::CLIMATE_MODE_OFF:
      trig = this->idle_trigger_;
      break;
    case climate::CLIMATE_MODE_COOL:
      trig = this->cool_trigger_;
      break;
    case climate::CLIMATE_MODE_HEAT:
      trig = this->heat_trigger_;
      break;
    default:
      trig = nullptr;
  }
  if (trig != nullptr) {
    // trig should never be null, but still check so that we don't crash
    trig->trigger();
    this->internal_mode_ = mode;
    this->prev_trigger_ = trig;
    this->publish_state();
  }
}
void BangBangClimate::change_away_(bool away) {
  if (!away) {
    this->target_temperature_low = this->normal_config_.default_temperature_low;
    this->target_temperature_high = this->normal_config_.default_temperature_high;
  } else {
    this->target_temperature_low = this->away_config_.default_temperature_low;
    this->target_temperature_high = this->away_config_.default_temperature_high;
  }
  this->away = away;
}
void BangBangClimate::set_normal_config(const BangBangClimateTargetTempConfig &normal_config) {
  this->normal_config_ = normal_config;
}
void BangBangClimate::set_away_config(const BangBangClimateTargetTempConfig &away_config) {
  this->supports_away_ = true;
  this->away_config_ = away_config;
}
BangBangClimate::BangBangClimate()
    : idle_trigger_(new Trigger<>()), cool_trigger_(new Trigger<>()), heat_trigger_(new Trigger<>()) {}
void BangBangClimate::set_sensor(sensor::Sensor *sensor) { this->sensor_ = sensor; }
Trigger<> *BangBangClimate::get_idle_trigger() const { return this->idle_trigger_; }
Trigger<> *BangBangClimate::get_cool_trigger() const { return this->cool_trigger_; }
void BangBangClimate::set_supports_cool(bool supports_cool) { this->supports_cool_ = supports_cool; }
Trigger<> *BangBangClimate::get_heat_trigger() const { return this->heat_trigger_; }
void BangBangClimate::set_supports_heat(bool supports_heat) { this->supports_heat_ = supports_heat; }

BangBangClimateTargetTempConfig::BangBangClimateTargetTempConfig() = default;
BangBangClimateTargetTempConfig::BangBangClimateTargetTempConfig(float default_temperature_low,
                                                                 float default_temperature_high)
    : default_temperature_low(default_temperature_low), default_temperature_high(default_temperature_high) {}

}  // namespace bang_bang
}  // namespace esphome
