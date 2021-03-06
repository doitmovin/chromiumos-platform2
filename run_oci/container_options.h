// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUN_OCI_CONTAINER_OPTIONS_H_
#define RUN_OCI_CONTAINER_OPTIONS_H_

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>

namespace run_oci {

using BindMount = std::pair<base::FilePath, base::FilePath>;
using BindMounts = std::vector<BindMount>;

struct ContainerOptions {
  std::string alt_syscall_table;
  BindMounts bind_mounts;
  std::string cgroup_parent;
  std::vector<std::string> extra_program_args;
  bool use_current_user;
  bool use_signatures;

  ContainerOptions() :
    alt_syscall_table(),
    bind_mounts(),
    cgroup_parent(),
    extra_program_args(),
    use_current_user(false),
    use_signatures(true) {}
};

}  // namespace run_oci

#endif  // RUN_OCI_CONTAINER_OPTIONS_H_
