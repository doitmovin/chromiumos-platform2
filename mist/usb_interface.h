// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_USB_INTERFACE_H_
#define MIST_USB_INTERFACE_H_

#include <base/basictypes.h>
#include <base/memory/scoped_ptr.h>
#include <base/memory/weak_ptr.h>

struct libusb_interface;

namespace mist {

class UsbDevice;
class UsbInterfaceDescriptor;

// A USB interface, which wraps a libusb_interface C struct from libusb 1.0 into
// a C++ object.
class UsbInterface {
 public:
  // Constructs a UsbInterface object by taking a weak pointer to a UsbDevice
  // object as |device| and a raw pointer to a libusb_interface struct as
  // |interface|. |device| is passed to the constructor of
  // UsbInterfaceDescriptor when creating a UsbInterfaceDescriptor object. The
  // ownership of |interface| is not transferred, and thus it should outlive
  // this object.
  UsbInterface(const base::WeakPtr<UsbDevice>& device,
               const libusb_interface* interface);

  ~UsbInterface();

  // Getters for retrieving fields of the libusb_interface struct.
  int GetNumAlternateSettings() const;

  // Returns a scoped pointer to a UsbInterfaceDescriptor object for the
  // interface descriptor indexed at |index|, or null scoped pointer if the
  // index is invalid. The returned object becomes invalid, and thus should not
  // be held, beyond the lifetime of this object.
  scoped_ptr<UsbInterfaceDescriptor> GetAlternateSetting(int index) const;

 private:
  base::WeakPtr<UsbDevice> device_;
  const libusb_interface* const interface_;

  DISALLOW_COPY_AND_ASSIGN(UsbInterface);
};

}  // namespace mist

#endif  // MIST_USB_INTERFACE_H_
