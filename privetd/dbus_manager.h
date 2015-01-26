// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIVETD_DBUS_MANAGER_H_
#define PRIVETD_DBUS_MANAGER_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <chromeos/dbus/async_event_sequencer.h>
#include <chromeos/dbus/dbus_object.h>
#include <chromeos/errors/error.h>
#include <chromeos/variant_dictionary.h>
#include <dbus/object_path.h>

#include "privetd/org.chromium.privetd.Manager.h"

namespace chromeos {
namespace dbus_utils {
class ExportedObjectManager;
}  // dbus_utils
}  // chromeos

namespace privetd {

// Exposes most of the privetd DBus interface.
class DBusManager : public org::chromium::privetd::ManagerInterface {
 public:
  using CompletionAction =
      chromeos::dbus_utils::AsyncEventSequencer::CompletionAction;

  explicit DBusManager(
      chromeos::dbus_utils::ExportedObjectManager* object_manager);
  ~DBusManager() override = default;
  void RegisterAsync(const CompletionAction& on_done);

  // DBus handlers
  bool EnableWiFiBootstrapping(
      chromeos::ErrorPtr* error,
      const dbus::ObjectPath& in_listener_path,
      const chromeos::VariantDictionary& in_options) override;
  bool DisableWiFiBootstrapping(chromeos::ErrorPtr* error) override;
  bool EnableGCDBootstrapping(
      chromeos::ErrorPtr* error,
      const dbus::ObjectPath& in_listener_path,
      const chromeos::VariantDictionary& in_options) override;
  bool DisableGCDBootstrapping(chromeos::ErrorPtr* error) override;
  void SetName(const std::string& in_name) override;
  void SetDescription(const std::string& in_description) override;
  std::string Ping() override;

 private:
  org::chromium::privetd::ManagerAdaptor dbus_adaptor_{this};
  std::unique_ptr<chromeos::dbus_utils::DBusObject> dbus_object_;

  DISALLOW_COPY_AND_ASSIGN(DBusManager);
};

}  // namespace privetd

#endif  // PRIVETD_DBUS_MANAGER_H_