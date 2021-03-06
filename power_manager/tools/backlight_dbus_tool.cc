// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <memory>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <brillo/flag_helper.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace {

// Queries the brightness from |proxy| and saves it to |percent|, returning true
// on success.
bool GetCurrentBrightness(dbus::ObjectProxy* proxy, double* percent) {
  dbus::MethodCall method_call(
      power_manager::kPowerManagerInterface,
      power_manager::kGetScreenBrightnessPercentMethod);
  std::unique_ptr<dbus::Response> response(proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));
  if (!response)
    return false;
  dbus::MessageReader reader(response.get());
  return reader.PopDouble(percent);
}

// Asks |proxy| to set the brightness to |percent| using |style|, returning true
// on success.
bool SetCurrentBrightness(dbus::ObjectProxy* proxy, double percent, int style) {
  dbus::MethodCall method_call(
      power_manager::kPowerManagerInterface,
      power_manager::kSetScreenBrightnessPercentMethod);
  dbus::MessageWriter writer(&method_call);
  writer.AppendDouble(percent);
  writer.AppendInt32(style);
  std::unique_ptr<dbus::Response> response(proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));
  return response.get() != NULL;
}

}  // namespace

// A tool to talk to powerd and get or set the backlight level.
int main(int argc, char* argv[]) {
  DEFINE_bool(decrease, false, "Decrease the brightness by one step");
  DEFINE_bool(increase, false, "Increase the brightness by one step");
  DEFINE_bool(set, false, "Set the brightness to --percent");
  DEFINE_double(percent, 0, "Percent to set, in the range [0.0, 100.0]");
  DEFINE_bool(gradual, true, "Transition gradually");
  DEFINE_bool(decrease_keyboard, false,
              "Decrease the keyboard brightness by one step");
  DEFINE_bool(increase_keyboard, false,
              "Increase the keyboard brightness by one step");

  brillo::FlagHelper::Init(
      argc, argv, "Query or change the panel backlight brightness via powerd.");

  CHECK_LE(FLAGS_decrease +
           FLAGS_increase +
           FLAGS_set +
           FLAGS_decrease_keyboard +
           FLAGS_increase_keyboard,
           1)
      << "You cannot set more than one action flag";

  base::AtExitManager at_exit_manager;
  base::MessageLoopForIO message_loop;

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(options));
  CHECK(bus->Connect());
  dbus::ObjectProxy* powerd_proxy = bus->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));

  if (FLAGS_decrease || FLAGS_increase) {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        FLAGS_decrease ? power_manager::kDecreaseScreenBrightnessMethod
                       : power_manager::kIncreaseScreenBrightnessMethod);
    if (FLAGS_decrease) {
      dbus::MessageWriter writer(&method_call);
      writer.AppendBool(true);  // allow_off
    }
    CHECK(powerd_proxy->CallMethodAndBlock(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));
  } else if (FLAGS_decrease_keyboard || FLAGS_increase_keyboard) {
    dbus::MethodCall method_call(
        power_manager::kPowerManagerInterface,
        FLAGS_decrease_keyboard ?
            power_manager::kDecreaseKeyboardBrightnessMethod :
            power_manager::kIncreaseKeyboardBrightnessMethod);
    CHECK(powerd_proxy->CallMethodAndBlock(
            &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT));
  } else {
    double percent = 0.0;
    CHECK(GetCurrentBrightness(powerd_proxy, &percent));
    printf("Current percent = %f\n", percent);
    if (FLAGS_set) {
      const int style = FLAGS_gradual
                            ? power_manager::kBrightnessTransitionGradual
                            : power_manager::kBrightnessTransitionInstant;
      CHECK(SetCurrentBrightness(powerd_proxy, FLAGS_percent, style));
      printf("Set percent to %f\n", FLAGS_percent);
      CHECK(GetCurrentBrightness(powerd_proxy, &percent));
      printf("Current percent now = %f\n", percent);
    }
  }

  return 0;
}
