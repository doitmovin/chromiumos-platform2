// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/keyboard_backlight_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/backlight_interface.h"

namespace power_manager {
namespace policy {

namespace {

// This is how long after a video playing message is received we should wait
// until reverting to the not playing state. If another message is received in
// this interval the timeout is reset. The browser should be sending these
// messages ~5 seconds when video is playing.
const int64_t kVideoTimeoutIntervalMs = 7000;

// Returns the total duration for |style|.
base::TimeDelta GetTransitionDuration(
    BacklightController::Transition transition) {
  switch (transition) {
    case BacklightController::Transition::INSTANT:
      return base::TimeDelta();
    case BacklightController::Transition::FAST:
      return base::TimeDelta::FromMilliseconds(kFastBacklightTransitionMs);
    case BacklightController::Transition::SLOW:
      return base::TimeDelta::FromMilliseconds(kSlowBacklightTransitionMs);
  }
  NOTREACHED() << "Unhandled transition style " << transition;
  return base::TimeDelta();
}

}  // namespace

KeyboardBacklightController::TestApi::TestApi(
    KeyboardBacklightController* controller)
    : controller_(controller) {}

KeyboardBacklightController::TestApi::~TestApi() {}

bool KeyboardBacklightController::TestApi::TriggerTurnOffTimeout() {
  if (!controller_->turn_off_timer_.IsRunning())
    return false;

  controller_->turn_off_timer_.Stop();
  controller_->UpdateState(Transition::SLOW, BrightnessChangeCause::AUTOMATED);
  return true;
}

bool KeyboardBacklightController::TestApi::TriggerVideoTimeout() {
  if (!controller_->video_timer_.IsRunning())
    return false;

  controller_->video_timer_.Stop();
  controller_->HandleVideoTimeout();
  return true;
}

const double KeyboardBacklightController::kDimPercent = 10.0;

KeyboardBacklightController::KeyboardBacklightController()
    : clock_(new Clock) {}

KeyboardBacklightController::~KeyboardBacklightController() {
  if (display_backlight_controller_)
    display_backlight_controller_->RemoveObserver(this);
}

void KeyboardBacklightController::Init(
    system::BacklightInterface* backlight,
    PrefsInterface* prefs,
    system::AmbientLightSensorInterface* sensor,
    BacklightController* display_backlight_controller,
    TabletMode initial_tablet_mode) {
  backlight_ = backlight;
  prefs_ = prefs;
  tablet_mode_ = initial_tablet_mode;

  display_backlight_controller_ = display_backlight_controller;
  if (display_backlight_controller_)
    display_backlight_controller_->AddObserver(this);

  if (sensor) {
    ambient_light_handler_.reset(new AmbientLightHandler(sensor, this));
    ambient_light_handler_->set_name("keyboard");
  }

  prefs_->GetBool(kDetectHoverPref, &supports_hover_);
  prefs_->GetBool(kKeyboardBacklightTurnOnForUserActivityPref,
                  &turn_on_for_user_activity_);

  int64_t delay_ms = 0;
  CHECK(prefs->GetInt64(kKeyboardBacklightKeepOnMsPref, &delay_ms));
  keep_on_delay_ = base::TimeDelta::FromMilliseconds(delay_ms);
  CHECK(prefs->GetInt64(kKeyboardBacklightKeepOnDuringVideoMsPref, &delay_ms));
  keep_on_during_video_delay_ = base::TimeDelta::FromMilliseconds(delay_ms);

  max_level_ = backlight_->GetMaxBrightnessLevel();
  current_level_ = backlight_->GetCurrentBrightnessLevel();
  LOG(INFO) << "Backlight has range [0, " << max_level_ << "] with initial "
            << "level " << current_level_;

  // Read the user-settable brightness steps (one per line).
  std::string input_str;
  if (!prefs_->GetString(kKeyboardBacklightUserStepsPref, &input_str))
    LOG(FATAL) << "Failed to read pref " << kKeyboardBacklightUserStepsPref;
  std::vector<std::string> lines = base::SplitString(
      input_str, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  for (std::vector<std::string>::iterator iter = lines.begin();
       iter != lines.end();
       ++iter) {
    double new_step = 0.0;
    if (!base::StringToDouble(*iter, &new_step))
      LOG(FATAL) << "Invalid line in pref " << kKeyboardBacklightUserStepsPref
                 << ": \"" << *iter << "\"";
    user_steps_.push_back(util::ClampPercent(new_step));
  }
  CHECK(!user_steps_.empty()) << "No user brightness steps defined in "
                              << kKeyboardBacklightUserStepsPref;

  if (ambient_light_handler_.get()) {
    std::string pref_value;
    CHECK(prefs_->GetString(kKeyboardBacklightAlsStepsPref, &pref_value))
        << "Unable to read pref " << kKeyboardBacklightAlsStepsPref;
    ambient_light_handler_->Init(pref_value, LevelToPercent(current_level_));
  } else {
    automated_percent_ = user_steps_.back();
    prefs_->GetDouble(kKeyboardBacklightNoAlsBrightnessPref,
                      &automated_percent_);
    UpdateState(Transition::SLOW, BrightnessChangeCause::AUTOMATED);
  }
}

void KeyboardBacklightController::AddObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void KeyboardBacklightController::RemoveObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void KeyboardBacklightController::HandlePowerSourceChange(PowerSource source) {}

void KeyboardBacklightController::HandleDisplayModeChange(DisplayMode mode) {}

void KeyboardBacklightController::HandleSessionStateChange(SessionState state) {
  session_state_ = state;
  if (state == SessionState::STARTED) {
    num_als_adjustments_ = 0;
    num_user_adjustments_ = 0;
  }
}

void KeyboardBacklightController::HandlePowerButtonPress() {}

void KeyboardBacklightController::HandleUserActivity(UserActivityType type) {
  last_user_activity_time_ = clock_->GetCurrentTime();
  UpdateTurnOffTimer();
  UpdateState(Transition::FAST, BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::HandleVideoActivity(bool is_fullscreen) {
  // Ignore fullscreen video that's reported when the user isn't logged in;
  // it may be triggered by animations on the login screen.
  if (is_fullscreen && session_state_ == SessionState::STOPPED)
    is_fullscreen = false;

  if (is_fullscreen != fullscreen_video_playing_) {
    VLOG(1) << "Fullscreen video "
            << (is_fullscreen ? "started" : "went non-fullscreen");
    fullscreen_video_playing_ = is_fullscreen;
    UpdateState(Transition::SLOW, BrightnessChangeCause::AUTOMATED);
  }

  video_timer_.Stop();
  if (is_fullscreen) {
    video_timer_.Start(
        FROM_HERE,
        base::TimeDelta::FromMilliseconds(kVideoTimeoutIntervalMs),
        this,
        &KeyboardBacklightController::HandleVideoTimeout);
  }
}

void KeyboardBacklightController::HandleHoverStateChange(bool hovering) {
  if (!supports_hover_ || hovering == hovering_)
    return;

  hovering_ = hovering;

  turn_off_timer_.Stop();
  if (!hovering_) {
    // If the user stopped hovering, start a timer to turn the backlight off in
    // a little while. If this is updated to do something else instead of
    // calling UpdateState(), TestApi::TriggerTurnOffTimeout() must also be
    // updated.
    last_hover_time_ = clock_->GetCurrentTime();
    UpdateTurnOffTimer();
  } else {
    last_hover_time_ = base::TimeTicks();
  }

  UpdateState(hovering_ ? Transition::FAST : Transition::SLOW,
              BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::HandleTabletModeChange(TabletMode mode) {
  if (mode == tablet_mode_)
    return;

  tablet_mode_ = mode;
  UpdateState(Transition::FAST, BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::HandlePolicyChange(
    const PowerManagementPolicy& policy) {}

void KeyboardBacklightController::HandleChromeStart() {}

void KeyboardBacklightController::SetDimmedForInactivity(bool dimmed) {
  if (dimmed == dimmed_for_inactivity_)
    return;
  dimmed_for_inactivity_ = dimmed;
  UpdateState(Transition::SLOW, BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::SetOffForInactivity(bool off) {
  if (off == off_for_inactivity_)
    return;
  off_for_inactivity_ = off;
  UpdateState(Transition::SLOW, BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::SetSuspended(bool suspended) {
  if (suspended == suspended_)
    return;
  suspended_ = suspended;
  UpdateState(suspended ? Transition::INSTANT : Transition::FAST,
              BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::SetShuttingDown(bool shutting_down) {
  if (shutting_down == shutting_down_)
    return;
  shutting_down_ = shutting_down;
  UpdateState(Transition::INSTANT, BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::SetDocked(bool docked) {
  if (docked == docked_)
    return;
  docked_ = docked;
  UpdateState(Transition::INSTANT, BrightnessChangeCause::AUTOMATED);
}

void KeyboardBacklightController::SetForcedOff(bool forced_off) {
  if (forced_off_ == forced_off)
    return;
  forced_off_ = forced_off;
  UpdateState(Transition::INSTANT, BrightnessChangeCause::AUTOMATED);
}

bool KeyboardBacklightController::GetForcedOff() {
  return forced_off_;
}

bool KeyboardBacklightController::GetBrightnessPercent(double* percent) {
  DCHECK(percent);
  *percent = LevelToPercent(current_level_);
  return *percent >= 0.0;
}

bool KeyboardBacklightController::SetUserBrightnessPercent(
    double percent, Transition transition) {
  // There's currently no UI for setting the keyboard backlight brightness
  // to arbitrary levels; the user is instead just given the option of
  // increasing or decreasing the brightness between pre-defined levels.
  return false;
}

bool KeyboardBacklightController::IncreaseUserBrightness() {
  LOG(INFO) << "Got user-triggered request to increase brightness";
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ < static_cast<int>(user_steps_.size()) - 1)
    user_step_index_++;
  num_user_adjustments_++;

  return UpdateState(Transition::FAST, BrightnessChangeCause::USER_INITIATED);
}

bool KeyboardBacklightController::DecreaseUserBrightness(bool allow_off) {
  LOG(INFO) << "Got user-triggered request to decrease brightness";
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ > (allow_off ? 0 : 1))
    user_step_index_--;
  num_user_adjustments_++;

  return UpdateState(Transition::FAST, BrightnessChangeCause::USER_INITIATED);
}

int KeyboardBacklightController::GetNumAmbientLightSensorAdjustments() const {
  return num_als_adjustments_;
}

int KeyboardBacklightController::GetNumUserAdjustments() const {
  return num_user_adjustments_;
}

void KeyboardBacklightController::SetBrightnessPercentForAmbientLight(
    double brightness_percent,
    AmbientLightHandler::BrightnessChangeCause cause) {
  automated_percent_ = brightness_percent;
  Transition transition =
      cause == AmbientLightHandler::BrightnessChangeCause::AMBIENT_LIGHT
          ? Transition::SLOW
          : Transition::FAST;
  if (UpdateState(transition, BrightnessChangeCause::AUTOMATED) &&
      cause == AmbientLightHandler::BrightnessChangeCause::AMBIENT_LIGHT)
    num_als_adjustments_++;
}

void KeyboardBacklightController::OnBrightnessChange(
    double brightness_percent,
    BacklightController::BrightnessChangeCause cause,
    BacklightController* source) {
  DCHECK_EQ(source, display_backlight_controller_);

  bool zero = brightness_percent <= kEpsilon;
  if (zero != display_brightness_is_zero_) {
    display_brightness_is_zero_ = zero;
    UpdateState(Transition::SLOW, cause);
  }
}

void KeyboardBacklightController::HandleVideoTimeout() {
  if (fullscreen_video_playing_)
    VLOG(1) << "Fullscreen video stopped";
  fullscreen_video_playing_ = false;
  UpdateState(Transition::FAST, BrightnessChangeCause::AUTOMATED);
  UpdateTurnOffTimer();
}

int64_t KeyboardBacklightController::PercentToLevel(double percent) const {
  if (max_level_ == 0)
    return -1;
  percent = std::max(std::min(percent, 100.0), 0.0);
  return lround(static_cast<double>(max_level_) * percent / 100.0);
}

double KeyboardBacklightController::LevelToPercent(int64_t level) const {
  if (max_level_ == 0)
    return -1.0;
  level = std::max(std::min(level, max_level_), static_cast<int64_t>(0));
  return static_cast<double>(level) * 100.0 / max_level_;
}

bool KeyboardBacklightController::RecentlyHoveringOrUserActive() const {
  if (hovering_)
    return true;

  const base::TimeTicks now = clock_->GetCurrentTime();
  const base::TimeDelta delay =
      fullscreen_video_playing_ ? keep_on_during_video_delay_ : keep_on_delay_;
  return (!last_hover_time_.is_null() && (now - last_hover_time_ < delay)) ||
         (!last_user_activity_time_.is_null() &&
          (now - last_user_activity_time_ < delay));
}

void KeyboardBacklightController::InitUserStepIndex() {
  if (user_step_index_ != -1)
    return;

  // Find the step nearest to the current backlight level.
  double percent = LevelToPercent(current_level_);
  double percent_delta = std::numeric_limits<double>::max();
  for (size_t i = 0; i < user_steps_.size(); i++) {
    double temp_delta = fabs(percent - user_steps_[i]);
    if (temp_delta < percent_delta) {
      percent_delta = temp_delta;
      user_step_index_ = i;
    }
  }
  CHECK_NE(user_step_index_, -1) << "Failed to find brightness step for level "
                                 << current_level_;
}

void KeyboardBacklightController::UpdateTurnOffTimer() {
  if (!supports_hover_ && !turn_on_for_user_activity_)
    return;

  turn_off_timer_.Stop();

  // The timer shouldn't start until hovering stops.
  if (hovering_)
    return;

  // Determine how much time is left.
  const base::TimeTicks timeout_start =
      std::max(last_hover_time_, last_user_activity_time_);
  if (timeout_start.is_null())
    return;

  const base::TimeDelta full_delay =
      fullscreen_video_playing_ ? keep_on_during_video_delay_ : keep_on_delay_;
  const base::TimeDelta remaining_delay =
      full_delay - (clock_->GetCurrentTime() - timeout_start);
  if (remaining_delay <= base::TimeDelta::FromMilliseconds(0))
    return;

  turn_off_timer_.Start(
      FROM_HERE,
      remaining_delay,
      base::Bind(base::IgnoreResult(&KeyboardBacklightController::UpdateState),
                 base::Unretained(this),
                 Transition::SLOW,
                 BrightnessChangeCause::AUTOMATED));
}

bool KeyboardBacklightController::UpdateState(Transition transition,
                                              BrightnessChangeCause cause) {
  // Force the backlight off immediately in several special cases.
  if (forced_off_ || shutting_down_ || docked_ || suspended_ ||
      tablet_mode_ == TabletMode::ON)
    return ApplyBrightnessPercent(0.0, transition, cause);

  // If the user has asked for a specific brightness level, use it unless the
  // user is inactive.
  if (user_step_index_ != -1) {
    double percent = user_steps_[user_step_index_];
    if ((off_for_inactivity_ || dimmed_for_inactivity_) && !hovering_)
      percent = off_for_inactivity_ ? 0.0 : std::min(kDimPercent, percent);
    return ApplyBrightnessPercent(percent, transition, cause);
  }

  // If requested, force the backlight on if the user is currently or was
  // recently active and off otherwise.
  if (supports_hover_ || turn_on_for_user_activity_) {
    if (RecentlyHoveringOrUserActive())
      return ApplyBrightnessPercent(automated_percent_, transition, cause);
    else
      return ApplyBrightnessPercent(0.0, transition, cause);
  }

  // Force the backlight off for several more lower-priority conditions.
  // TODO(derat): Restructure this so the backlight is kept on for at least a
  // short period after hovering stops while fullscreen video is playing:
  // http://crbug.com/623404.
  if (fullscreen_video_playing_ || display_brightness_is_zero_ ||
      off_for_inactivity_) {
    return ApplyBrightnessPercent(0.0, transition, cause);
  }

  if (dimmed_for_inactivity_) {
    return ApplyBrightnessPercent(
        std::min(kDimPercent, automated_percent_), transition, cause);
  }

  return ApplyBrightnessPercent(automated_percent_, transition, cause);
}

bool KeyboardBacklightController::ApplyBrightnessPercent(
    double percent, Transition transition, BrightnessChangeCause cause) {
  int64_t level = PercentToLevel(percent);
  if (level == current_level_ && !backlight_->TransitionInProgress())
    return false;

  base::TimeDelta interval = GetTransitionDuration(transition);
  LOG(INFO) << "Setting brightness to " << level << " (" << percent
            << "%) over " << interval.InMilliseconds() << " ms";
  if (!backlight_->SetBrightnessLevel(level, interval)) {
    LOG(ERROR) << "Failed to set brightness";
    return false;
  }

  current_level_ = level;
  FOR_EACH_OBSERVER(BacklightControllerObserver,
                    observers_,
                    OnBrightnessChange(percent, cause, this));
  return true;
}

}  // namespace policy
}  // namespace power_manager
