// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTILS_H_
#define UTILS_H_

#include <fstream>
#include <vector>

#include "base/basictypes.h"

bool FileToBuffer(const std::string& filename, std::vector<char>* contents);

bool BufferToFile(const std::string& filename,
                  const std::vector<char>& contents);

std::ifstream::pos_type GetFileSize(const std::string& filename);

bool CompareFileContents(const std::string& a, const std::string& b);

uint64 Md5Prefix(const std::string& input);

bool CreateNamedTempFile(std::string* name);

// Returns true if the perf reports show the same summary.  Metadata is not
// compared.
bool ComparePerfReports(const std::string& a, const std::string& b);

#endif  // UTILS_H_
