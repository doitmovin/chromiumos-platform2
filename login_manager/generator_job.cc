// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class is most definitely NOT re-entrant.

#include "login_manager/generator_job.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/file_path.h>
#include <base/logging.h>
#include <base/string_util.h>

#include "login_manager/system_utils.h"

namespace login_manager {
namespace {
const char kKeygenExecutable[] = "/sbin/keygen";
}  // namespace

GeneratorJobFactoryInterface::~GeneratorJobFactoryInterface() {}

GeneratorJob::Factory::Factory() {}
GeneratorJob::Factory::~Factory() {}

scoped_ptr<GeneratorJobInterface> GeneratorJob::Factory::Create(
    const std::string& filename,
    const base::FilePath& user_path,
    uid_t desired_uid,
    SystemUtils* utils) {
  return scoped_ptr<GeneratorJobInterface>(new GeneratorJob(filename,
                                                            user_path,
                                                            desired_uid,
                                                            utils));
}

GeneratorJob::GeneratorJob(const std::string& filename,
                           const base::FilePath& user_path,
                           uid_t desired_uid,
                           SystemUtils* utils)
    : filename_(filename),
      user_path_(user_path.value()),
      system_(utils),
      subprocess_(desired_uid, system_) {
}

GeneratorJob::~GeneratorJob() {}

bool GeneratorJob::RunInBackground() {
  char const* argv[4];
  argv[0] = kKeygenExecutable;
  argv[1] = filename_.c_str();
  argv[2] = user_path_.c_str();
  argv[3] = NULL;

  return subprocess_.ForkAndExec(argv);
}

void GeneratorJob::KillEverything(int signal, const std::string& message) {
  if (subprocess_.pid() < 0)
    return;
  subprocess_.KillEverything(signal);
}

void GeneratorJob::Kill(int signal, const std::string& message) {
  if (subprocess_.pid() < 0)
    return;
  subprocess_.Kill(signal);
}

const std::string GeneratorJob::GetName() const {
  FilePath exec_file(kKeygenExecutable);
  return exec_file.BaseName().value();
}

}  // namespace login_manager
