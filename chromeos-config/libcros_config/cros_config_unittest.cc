// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* Libary to provide access to the Chrome OS master configuration */

#include <base/files/file_path.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <gtest/gtest.h>

class CrosConfigTest : public testing::Test {
 protected:
  void InitConfig() {
    base::FilePath filepath("test.dtb");
    ASSERT_TRUE(cros_config_.InitForTest(filepath, "pyro"));
  }

  brillo::CrosConfig cros_config_;
};

TEST_F(CrosConfigTest, CheckMissingFile) {
  base::FilePath filepath("invalid-file");
  ASSERT_FALSE(cros_config_.InitForTest(filepath, "pyro"));
}

TEST_F(CrosConfigTest, CheckBadFile) {
  base::FilePath filepath("test.dts");
  ASSERT_FALSE(cros_config_.InitForTest(filepath, "pyro"));
}

TEST_F(CrosConfigTest, CheckBadStruct) {
  base::FilePath filepath("test_bad_struct.dtb");
  ASSERT_FALSE(cros_config_.InitForTest(filepath, "pyto"));
}

TEST_F(CrosConfigTest, CheckUnknownModel) {
  base::FilePath filepath("test.dtb");
  ASSERT_FALSE(cros_config_.InitForTest(filepath, "no-model"));
}

TEST_F(CrosConfigTest, Check111NoInit) {
  std::string val;
  ASSERT_FALSE(cros_config_.GetString("/", "wallpaper", &val));
}

TEST_F(CrosConfigTest, CheckWrongPath) {
  InitConfig();
  std::string val;
  ASSERT_FALSE(cros_config_.GetString("/wibble", "wallpaper", &val));
}

TEST_F(CrosConfigTest, CheckBadString) {
  InitConfig();
  std::string val;
  ASSERT_FALSE(cros_config_.GetString("/", "string-list", &val));
}

TEST_F(CrosConfigTest, CheckCorrectModel) {
  InitConfig();
  std::string val;
  ASSERT_TRUE(cros_config_.GetString("/", "wallpaper", &val));
  ASSERT_EQ("default", val);
}

int main(int argc, char **argv) {
  int status = system("exec ./chromeos-config-test-setup.sh");
  if (status != 0)
    return EXIT_FAILURE;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
