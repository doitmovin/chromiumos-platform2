// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SALSA_TRY_TOUCH_EXPERIMENT_TREATMENT_H_
#define SALSA_TRY_TOUCH_EXPERIMENT_TREATMENT_H_

#include <string>
#include <vector>

#include <base/strings/string_split.h>

#include "salsa/try_touch_experiment/property.h"

class Treatment {
 public:
  Treatment();
  explicit Treatment(const std::string &treatment_string);

  bool Apply() const;
  bool Reset() const;

  bool valid() const;

 private:
  std::vector<Property> properties_;
  bool is_valid_;
};

#endif  // SALSA_TRY_TOUCH_EXPERIMENT_TREATMENT_H_
