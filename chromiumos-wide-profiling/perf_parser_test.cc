// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <string>

#include "base/logging.h"

#include "perf_parser.h"
#include "perf_reader.h"
#include "perf_test_files.h"
#include "quipper_string.h"
#include "utils.h"

namespace quipper {

namespace {

void CheckChronologicalOrderOfEvents(const std::vector<ParsedEvent*>& events) {
  for (unsigned int i = 1; i < events.size(); ++i) {
    uint64 time = events[i]->sample_info->time;
    uint64 prev_time = events[i - 1]->sample_info->time;
    CHECK_LE(prev_time, time);
  }
}

}  // namespace

TEST(PerfParserTest, Test1Cycle) {
  for (unsigned int i = 0;
       i < arraysize(perf_test_files::kPerfDataFiles);
       ++i) {
    string input_perf_data = perf_test_files::kPerfDataFiles[i];
    LOG(INFO) << "Testing " << input_perf_data;

    PerfParser parser;
    ASSERT_TRUE(parser.ReadFile(input_perf_data));
    parser.ParseRawEvents();

    CHECK_GT(parser.GetEventsSortedByTime().size(), 0U);
    CheckChronologicalOrderOfEvents(parser.GetEventsSortedByTime());

    // Check perf event stats.
    const PerfEventStats& stats = parser.stats();
    EXPECT_GT(stats.num_sample_events, 0U);
    EXPECT_GT(stats.num_mmap_events, 0U);
    EXPECT_GT(stats.num_sample_events_mapped, 0U);
    EXPECT_FALSE(stats.did_remap);

    string output_perf_data = input_perf_data + ".parse.out";
    ASSERT_TRUE(parser.WriteFile(output_perf_data));

    EXPECT_TRUE(ComparePerfReports(input_perf_data, output_perf_data));
  }
}

TEST(PerfParserTest, TestRemap) {
  for (unsigned int i = 0;
       i < arraysize(perf_test_files::kPerfDataFiles);
       ++i) {
    string input_perf_data = perf_test_files::kPerfDataFiles[i];
    LOG(INFO) << "Testing " << input_perf_data;

    PerfParser parser;
    parser.set_do_remap(true);
    ASSERT_TRUE(parser.ReadFile(input_perf_data));
    parser.ParseRawEvents();

    // Check perf event stats.
    const PerfEventStats& stats = parser.stats();
    EXPECT_GT(stats.num_sample_events, 0U);
    EXPECT_GT(stats.num_mmap_events, 0U);
    EXPECT_GT(stats.num_sample_events_mapped, 0U);
    EXPECT_TRUE(stats.did_remap);

    string output_perf_data = input_perf_data + ".parse.remap.out";
    ASSERT_TRUE(parser.WriteFile(output_perf_data));

    // Remapped addresses should not match the original addresses.
    EXPECT_FALSE(ComparePerfReports(input_perf_data, output_perf_data));

    PerfParser remap_parser;
    remap_parser.set_do_remap(true);
    ASSERT_TRUE(remap_parser.ReadFile(output_perf_data));
    remap_parser.ParseRawEvents();
    string output_perf_data2 = input_perf_data + ".parse.remap2.out";
    ASSERT_TRUE(remap_parser.WriteFile(output_perf_data2));

    // Remapping again should produce the same addresses.
    EXPECT_TRUE(ComparePerfReports(output_perf_data, output_perf_data2));
  }
}

}  // namespace quipper

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
