// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for UsersOldestActivityTimestampCache.

#include "cryptohome/user_oldest_activity_timestamp_cache.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <gtest/gtest.h>

using base::FilePath;

namespace {
const base::Time::Exploded jan1st2011_exploded = { 2011, 1, 6, 1 };
const base::Time time_jan1 = base::Time::FromUTCExploded(jan1st2011_exploded);
const base::Time::Exploded feb1st2011_exploded = { 2011, 2, 2, 1 };
const base::Time time_feb1 = base::Time::FromUTCExploded(feb1st2011_exploded);
const base::Time::Exploded mar1st2011_exploded = { 2011, 3, 2, 1 };
const base::Time time_mar1 = base::Time::FromUTCExploded(mar1st2011_exploded);
}  // namespace

namespace cryptohome {

TEST(UserOldestActivityTimestampCache, Sequential) {
  UserOldestActivityTimestampCache cache;
  cache.Initialize();

  // Fill the cache with users with different (or no) timestamp.
  // Check that the latest timestamp is actually oldest.
  cache.AddExistingUserNotime(FilePath("a"));
  EXPECT_TRUE(cache.oldest_known_timestamp().is_null());

  cache.AddExistingUser(FilePath("b"), time_mar1);
  EXPECT_FALSE(cache.oldest_known_timestamp().is_null());
  EXPECT_TRUE(time_mar1 == cache.oldest_known_timestamp());

  cache.AddExistingUser(FilePath("c"), time_jan1);
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());

  cache.AddExistingUser(FilePath("d"), time_feb1);
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());
  cache.UpdateExistingUser(FilePath("d"), time_mar1);
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());

  cache.AddExistingUserNotime(FilePath("e"));
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());

  // Remove users one by one, check the remaining oldest timestamp.
  EXPECT_EQ("a", cache.RemoveOldestUser().value());
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());

  EXPECT_EQ("e", cache.RemoveOldestUser().value());
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());

  EXPECT_EQ("c", cache.RemoveOldestUser().value());
  EXPECT_TRUE(time_mar1 == cache.oldest_known_timestamp());

  EXPECT_EQ("b", cache.RemoveOldestUser().value());
  EXPECT_TRUE(time_mar1 == cache.oldest_known_timestamp());

  EXPECT_EQ("d", cache.RemoveOldestUser().value());
  EXPECT_TRUE(cache.oldest_known_timestamp().is_null());
}

TEST(UserOldestActivityTimestampCache, OneUpdatedForward) {
  UserOldestActivityTimestampCache cache;
  cache.Initialize();
  EXPECT_TRUE(cache.oldest_known_timestamp().is_null());

  cache.AddExistingUser(FilePath("x"), time_feb1);
  EXPECT_FALSE(cache.oldest_known_timestamp().is_null());
  EXPECT_TRUE(time_feb1 == cache.oldest_known_timestamp());

  cache.UpdateExistingUser(FilePath("x"), time_mar1);
  EXPECT_TRUE(time_mar1 == cache.oldest_known_timestamp());

  EXPECT_EQ("x", cache.RemoveOldestUser().value());
  EXPECT_TRUE(cache.oldest_known_timestamp().is_null());
}

TEST(UserOldestActivityTimestampCache, OneUpdatedBackward) {
  UserOldestActivityTimestampCache cache;
  cache.Initialize();
  EXPECT_TRUE(cache.oldest_known_timestamp().is_null());

  cache.AddExistingUser(FilePath("x"), time_feb1);
  EXPECT_FALSE(cache.oldest_known_timestamp().is_null());
  EXPECT_TRUE(time_feb1 == cache.oldest_known_timestamp());

  cache.UpdateExistingUser(FilePath("x"), time_jan1);
  EXPECT_TRUE(time_jan1 == cache.oldest_known_timestamp());

  EXPECT_EQ("x", cache.RemoveOldestUser().value());
  EXPECT_TRUE(cache.oldest_known_timestamp().is_null());
}

}  // namespace cryptohome
