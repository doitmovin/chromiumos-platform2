// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <cstdlib>

#include "metrics/metrics_library.h"

enum Mode {
    kModeSendSample,
    kModeSendEnumSample,
    kModeSendSparseSample,
    kModeSendUserAction,
    kModeSendCrosEvent,
    kModeHasConsent,
    kModeIsGuestMode,
    kModeShowConsentId,
    kModeCreateConsent,
    kModeDeleteConsent,
};

void ShowUsage() {
  fprintf(stderr,
          "Usage:  metrics_client [-t] name sample min max nbuckets\n"
          "        metrics_client -e   name sample max\n"
          "        metrics_client -s   name sample\n"
          "        metrics_client -v   event\n"
          "        metrics_client -u action\n"
          "        metrics_client [-cCDg]\n"
          "\n"
          "  default: send metric with integer values\n"
          "           |min| > 0, |min| <= sample < |max|\n"
          "  -C: Create consent file such that -c will return 0.\n"
          "  -D: Delete consent file such that -c will return 1.\n"
          "  -c: return exit status 0 if user consents to stats, 1 otherwise,\n"
          "      in guest mode always return 1\n"
          "  -e: send linear/enumeration histogram data\n"
          "  -g: return exit status 0 if machine in guest mode, 1 otherwise\n"
          "  -s: send a sparse histogram sample\n"
          "  -t: convert sample from double seconds to int milliseconds\n"
          "  -u: send a user action\n"
          "  -v: send a Platform.CrOSEvent enum histogram sample\n");
  exit(1);
}

static int ParseInt(const char *arg) {
  char *endptr;
  int value = strtol(arg, &endptr, 0);
  if (*endptr != '\0') {
    fprintf(stderr, "metrics client: bad integer \"%s\"\n", arg);
    ShowUsage();
  }
  return value;
}

static double ParseDouble(const char *arg) {
  char *endptr;
  double value = strtod(arg, &endptr);
  if (*endptr != '\0') {
    fprintf(stderr, "metrics client: bad double \"%s\"\n", arg);
    ShowUsage();
  }
  return value;
}

static int SendStats(char* argv[],
                     int name_index,
                     enum Mode mode,
                     bool secs_to_msecs) {
  const char* name = argv[name_index];
  int sample;
  if (secs_to_msecs) {
    sample = static_cast<int>(ParseDouble(argv[name_index + 1]) * 1000.0);
  } else {
    sample = ParseInt(argv[name_index + 1]);
  }

  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  if (mode == kModeSendSparseSample) {
    metrics_lib.SendSparseToUMA(name, sample);
  } else if (mode == kModeSendEnumSample) {
    int max = ParseInt(argv[name_index + 2]);
    metrics_lib.SendEnumToUMA(name, sample, max);
  } else {
    int min = ParseInt(argv[name_index + 2]);
    int max = ParseInt(argv[name_index + 3]);
    int nbuckets = ParseInt(argv[name_index + 4]);
    metrics_lib.SendToUMA(name, sample, min, max, nbuckets);
  }
  return 0;
}

static int SendUserAction(char* argv[], int action_index) {
  const char* action = argv[action_index];
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  metrics_lib.SendUserActionToUMA(action);
  return 0;
}

static int SendCrosEvent(char* argv[], int action_index) {
  const char* event = argv[action_index];
  bool result;
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  result = metrics_lib.SendCrosEventToUMA(event);
  if (!result) {
    fprintf(stderr, "metrics_client: could not send event %s\n", event);
    return 1;
  }
  return 0;
}

static int CreateConsent() {
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  return metrics_lib.EnableMetrics() ? 0 : 1;
}

static int DeleteConsent() {
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  return metrics_lib.DisableMetrics() ? 0 : 1;
}

static int HasConsent() {
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  return metrics_lib.AreMetricsEnabled() ? 0 : 1;
}

static int IsGuestMode() {
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  return metrics_lib.IsGuestMode() ? 0 : 1;
}

static int ShowConsentId() {
  MetricsLibrary metrics_lib;
  metrics_lib.Init();
  std::string id;
  if (metrics_lib.ConsentId(&id) == false)
    return 1;
  printf("%s\n", id.c_str());
  return 0;
}

int main(int argc, char** argv) {
  enum Mode mode = kModeSendSample;
  bool secs_to_msecs = false;

  // Parse arguments
  int flag;
  while ((flag = getopt(argc, argv, "CDcegistuv")) != -1) {
    switch (flag) {
      case 'C':
        mode = kModeCreateConsent;
        break;
      case 'D':
        mode = kModeDeleteConsent;
        break;
      case 'c':
        mode = kModeHasConsent;
        break;
      case 'e':
        mode = kModeSendEnumSample;
        break;
      case 'g':
        mode = kModeIsGuestMode;
        break;
      case 'i':
        mode = kModeShowConsentId;
        break;
      case 's':
        mode = kModeSendSparseSample;
        break;
      case 't':
        secs_to_msecs = true;
        break;
      case 'u':
        mode = kModeSendUserAction;
        break;
      case 'v':
        mode = kModeSendCrosEvent;
        break;
      default:
        ShowUsage();
        break;
    }
  }
  int arg_index = optind;

  int expected_args = 0;
  if (mode == kModeSendSample)
    expected_args = 5;
  else if (mode == kModeSendEnumSample)
    expected_args = 3;
  else if (mode == kModeSendSparseSample)
    expected_args = 2;
  else if (mode == kModeSendUserAction)
    expected_args = 1;
  else if (mode == kModeSendCrosEvent)
    expected_args = 1;

  if ((arg_index + expected_args) != argc) {
    ShowUsage();
  }

  switch (mode) {
    case kModeSendSample:
    case kModeSendEnumSample:
    case kModeSendSparseSample:
      if ((mode != kModeSendSample) && secs_to_msecs) {
        ShowUsage();
      }
      return SendStats(argv,
                       arg_index,
                       mode,
                       secs_to_msecs);
    case kModeSendUserAction:
      return SendUserAction(argv, arg_index);
    case kModeSendCrosEvent:
      return SendCrosEvent(argv, arg_index);
    case kModeCreateConsent:
      return CreateConsent();
    case kModeDeleteConsent:
      return DeleteConsent();
    case kModeHasConsent:
      return HasConsent();
    case kModeIsGuestMode:
      return IsGuestMode();
    case kModeShowConsentId:
      return ShowConsentId();
    default:
      ShowUsage();
      return 0;
  }
}
