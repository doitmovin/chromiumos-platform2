// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_CONTEXT_H_
#define MIST_CONTEXT_H_

#include <memory>

#include <base/macros.h>

namespace mist {

class ConfigLoader;
class EventDispatcher;
class Metrics;
class Udev;
class UsbDeviceEventNotifier;
class UsbManager;

// A context class for holding the key helper objects used in mist, which
// simplifies the passing of the helper objects to other objects. For instance,
// instead of passing various helper objects to an object via its constructor,
// the context object is passed.
class Context {
 public:
  Context();
  virtual ~Context();

  // Initializes all helper objects in the context. Returns true on success.
  virtual bool Initialize();

  Metrics* metrics() const { return metrics_.get(); }
  ConfigLoader* config_loader() const { return config_loader_.get(); }
  EventDispatcher* event_dispatcher() const { return event_dispatcher_.get(); }
  Udev* udev() const { return udev_.get(); }
  UsbDeviceEventNotifier* usb_device_event_notifier() const {
    return usb_device_event_notifier_.get();
  }
  UsbManager* usb_manager() const { return usb_manager_.get(); }

 private:
  friend class MockContext;

  std::unique_ptr<Metrics> metrics_;
  std::unique_ptr<ConfigLoader> config_loader_;
  std::unique_ptr<EventDispatcher> event_dispatcher_;
  std::unique_ptr<Udev> udev_;
  std::unique_ptr<UsbDeviceEventNotifier> usb_device_event_notifier_;
  std::unique_ptr<UsbManager> usb_manager_;

  DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace mist

#endif  // MIST_CONTEXT_H_
