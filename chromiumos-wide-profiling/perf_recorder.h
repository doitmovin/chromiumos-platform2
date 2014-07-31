// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_RECORDER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_RECORDER_H_

#include <string>

#include "base/basictypes.h"

#include "chromiumos-wide-profiling/perf_reader.h"
#include "chromiumos-wide-profiling/quipper_proto.h"
#include "chromiumos-wide-profiling/quipper_string.h"

namespace quipper {

class PerfRecorder {
 public:
  PerfRecorder() {}
  bool RecordAndConvertToProtobuf(const string& perf_command,
                                  const int time,
                                  quipper::PerfDataProto* perf_data);
 private:
  string GetSleepCommand(const int time);
  DISALLOW_COPY_AND_ASSIGN(PerfRecorder);
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_RECORDER_H_
