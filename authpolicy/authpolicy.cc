// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>

#include "authpolicy/org.chromium.authpolicy.Domain.h"

using brillo::dbus_utils::AsyncEventSequencer;

namespace org {
namespace chromium {
namespace authpolicy {

const char kServiceName[] = "org.chromium.authpolicy";
const char kRootServicePath[] = "/org/chromium/authpolicy";

class Domain : public org::chromium::authpolicy::DomainInterface {
 public:
  explicit Domain(brillo::dbus_utils::ExportedObjectManager* object_manager)
      : dbus_object_{new brillo::dbus_utils::DBusObject{
        object_manager, object_manager->GetBus(),
            org::chromium::authpolicy::DomainAdaptor::GetObjectPath()}} {
      }

  ~Domain() override = default;
  bool Auth(
      brillo::ErrorPtr* error,
      const std::string& in_login,
      const dbus::FileDescriptor& in_password,
      int32_t* out_code,
      std::string* sid) override {
    return false;
  }

  bool Join(
      brillo::ErrorPtr* error,
      const std::string& in_machine_name,
      const std::string& in_login,
      const dbus::FileDescriptor& in_password,
      int32_t* out_code) override {
    return false;
  }

  void RegisterAsync(
      const AsyncEventSequencer::CompletionAction& completion_callback) {
    scoped_refptr<AsyncEventSequencer> sequencer(new AsyncEventSequencer());
    dbus_adaptor_.RegisterWithDBusObject(dbus_object_.get());
    dbus_object_->RegisterAsync(
              sequencer->GetHandler("Failed exporting Domain.", true));
    sequencer->OnAllTasksCompletedCall({completion_callback});
  }

 private:
  org::chromium::authpolicy::DomainAdaptor dbus_adaptor_{this};
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon() : DBusServiceDaemon(kServiceName, kRootServicePath) {
  }

 protected:
  void RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) override {
    domain_.reset(new Domain(object_manager_.get()));
    domain_->RegisterAsync(
        sequencer->GetHandler("Domain.RegisterAsync() failed.", true));
  }

  void OnShutdown(int* return_code) override {
    DBusServiceDaemon::OnShutdown(return_code);
    domain_.reset();
  }

 private:
  std::unique_ptr<Domain> domain_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace authpolicy
}  // namespace chromium
}  // namespace org

int main() {
  org::chromium::authpolicy::Daemon daemon;
  return daemon.Run();
}