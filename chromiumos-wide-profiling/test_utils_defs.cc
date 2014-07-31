// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/utils.h"

namespace quipper {

string GetTestInputFilePath(const string& filename) {
  return "testdata/" + filename;
}

string GetPerfPath() {
  return "/usr/bin/perf";
}

}  // namespace quipper
