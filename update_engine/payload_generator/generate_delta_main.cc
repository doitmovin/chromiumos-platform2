// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <set>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <chromeos/flag_helper.h>
#include <glib.h>

#include "update_engine/delta_performer.h"
#include "update_engine/payload_generator/delta_diff_generator.h"
#include "update_engine/payload_generator/payload_signer.h"
#include "update_engine/payload_verifier.h"
#include "update_engine/prefs.h"
#include "update_engine/subprocess.h"
#include "update_engine/terminator.h"
#include "update_engine/update_metadata.pb.h"
#include "update_engine/utils.h"

// This file contains a simple program that takes an old path, a new path,
// and an output file as arguments and the path to an output file and
// generates a delta that can be sent to Chrome OS clients.

using std::set;
using std::string;
using std::vector;

namespace chromeos_update_engine {

namespace {

// The minor version used by the in-place delta generator algorithm.
const uint32_t kInPlaceMinorPayloadVersion = 1;

void ParseSignatureSizes(const string& signature_sizes_flag,
                         vector<int>* signature_sizes) {
  signature_sizes->clear();
  vector<string> split_strings;

  base::SplitString(signature_sizes_flag, ':', &split_strings);
  for (const string& str : split_strings) {
    int size = 0;
    bool parsing_successful = base::StringToInt(str, &size);
    LOG_IF(FATAL, !parsing_successful)
        << "Invalid signature size: " << str;

    LOG_IF(FATAL, size != (2048 / 8)) <<
        "Only signature sizes of 256 bytes are supported.";

    signature_sizes->push_back(size);
  }
}


bool ParseImageInfo(const string& channel,
                    const string& board,
                    const string& version,
                    const string& key,
                    const string& build_channel,
                    const string& build_version,
                    ImageInfo* image_info) {
  // All of these arguments should be present or missing.
  bool empty = channel.empty();

  CHECK_EQ(channel.empty(), empty);
  CHECK_EQ(board.empty(), empty);
  CHECK_EQ(version.empty(), empty);
  CHECK_EQ(key.empty(), empty);

  if (empty)
    return false;

  image_info->set_channel(channel);
  image_info->set_board(board);
  image_info->set_version(version);
  image_info->set_key(key);

  image_info->set_build_channel(
      build_channel.empty() ? channel : build_channel);

  image_info->set_build_version(
      build_version.empty() ? version : build_version);

  return true;
}

void CalculatePayloadHashForSigning(const vector<int> &sizes,
                                    const string& out_hash_file,
                                    const string& in_file) {
  LOG(INFO) << "Calculating payload hash for signing.";
  LOG_IF(FATAL, in_file.empty())
      << "Must pass --in_file to calculate hash for signing.";
  LOG_IF(FATAL, out_hash_file.empty())
      << "Must pass --out_hash_file to calculate hash for signing.";

  chromeos::Blob hash;
  bool result = PayloadSigner::HashPayloadForSigning(in_file, sizes,
                                                     &hash);
  CHECK(result);

  result = utils::WriteFile(out_hash_file.c_str(), hash.data(), hash.size());
  CHECK(result);
  LOG(INFO) << "Done calculating payload hash for signing.";
}


void CalculateMetadataHashForSigning(const vector<int> &sizes,
                                     const string& out_metadata_hash_file,
                                     const string& in_file) {
  LOG(INFO) << "Calculating metadata hash for signing.";
  LOG_IF(FATAL, in_file.empty())
      << "Must pass --in_file to calculate metadata hash for signing.";
  LOG_IF(FATAL, out_metadata_hash_file.empty())
      << "Must pass --out_metadata_hash_file to calculate metadata hash.";

  chromeos::Blob hash;
  bool result = PayloadSigner::HashMetadataForSigning(in_file, sizes,
                                                      &hash);
  CHECK(result);

  result = utils::WriteFile(out_metadata_hash_file.c_str(), hash.data(),
                            hash.size());
  CHECK(result);

  LOG(INFO) << "Done calculating metadata hash for signing.";
}

void SignPayload(const string& in_file,
                 const string& out_file,
                 const string& signature_file) {
  LOG(INFO) << "Signing payload.";
  LOG_IF(FATAL, in_file.empty())
      << "Must pass --in_file to sign payload.";
  LOG_IF(FATAL, out_file.empty())
      << "Must pass --out_file to sign payload.";
  LOG_IF(FATAL, signature_file.empty())
      << "Must pass --signature_file to sign payload.";
  vector<chromeos::Blob> signatures;
  vector<string> signature_files;
  base::SplitString(signature_file, ':', &signature_files);
  for (const string& signature_file : signature_files) {
    chromeos::Blob signature;
    CHECK(utils::ReadFile(signature_file, &signature));
    signatures.push_back(signature);
  }
  uint64_t final_metadata_size;
  CHECK(PayloadSigner::AddSignatureToPayload(
      in_file, signatures, out_file, &final_metadata_size));
  LOG(INFO) << "Done signing payload. Final metadata size = "
            << final_metadata_size;
}

void VerifySignedPayload(const string& in_file,
                         const string& public_key,
                         int public_key_version) {
  LOG(INFO) << "Verifying signed payload.";
  LOG_IF(FATAL, in_file.empty())
      << "Must pass --in_file to verify signed payload.";
  LOG_IF(FATAL, public_key.empty())
      << "Must pass --public_key to verify signed payload.";
  CHECK(PayloadVerifier::VerifySignedPayload(in_file, public_key,
                                             public_key_version));
  LOG(INFO) << "Done verifying signed payload.";
}

void ApplyDelta(const string& in_file,
                const string& old_kernel,
                const string& old_image,
                const string& prefs_dir) {
  LOG(INFO) << "Applying delta.";
  LOG_IF(FATAL, old_image.empty())
      << "Must pass --old_image to apply delta.";
  Prefs prefs;
  InstallPlan install_plan;
  LOG(INFO) << "Setting up preferences under: " << prefs_dir;
  LOG_IF(ERROR, !prefs.Init(base::FilePath(prefs_dir)))
      << "Failed to initialize preferences.";
  // Get original checksums
  LOG(INFO) << "Calculating original checksums";
  PartitionInfo kern_info, root_info;
  CHECK(DeltaDiffGenerator::InitializePartitionInfo(true,  // is_kernel
                                                    old_kernel,
                                                    &kern_info));
  CHECK(DeltaDiffGenerator::InitializePartitionInfo(false,  // is_kernel
                                                    old_image,
                                                    &root_info));
  install_plan.kernel_hash.assign(kern_info.hash().begin(),
                                  kern_info.hash().end());
  install_plan.rootfs_hash.assign(root_info.hash().begin(),
                                  root_info.hash().end());
  DeltaPerformer performer(&prefs, nullptr, &install_plan);
  CHECK_EQ(performer.Open(old_image.c_str(), 0, 0), 0);
  CHECK(performer.OpenKernel(old_kernel.c_str()));
  chromeos::Blob buf(1024 * 1024);
  int fd = open(in_file.c_str(), O_RDONLY, 0);
  CHECK_GE(fd, 0);
  ScopedFdCloser fd_closer(&fd);
  for (off_t offset = 0;; offset += buf.size()) {
    ssize_t bytes_read;
    CHECK(utils::PReadAll(fd, buf.data(), buf.size(), offset, &bytes_read));
    if (bytes_read == 0)
      break;
    CHECK_EQ(performer.Write(buf.data(), bytes_read), bytes_read);
  }
  CHECK_EQ(performer.Close(), 0);
  DeltaPerformer::ResetUpdateProgress(&prefs, false);
  LOG(INFO) << "Done applying delta.";
}

int Main(int argc, char** argv) {
  DEFINE_string(old_dir, "",
                "Directory where the old rootfs is loop mounted read-only");
  DEFINE_string(new_dir, "",
                "Directory where the new rootfs is loop mounted read-only");
  DEFINE_string(old_image, "", "Path to the old rootfs");
  DEFINE_string(new_image, "", "Path to the new rootfs");
  DEFINE_string(old_kernel, "", "Path to the old kernel partition image");
  DEFINE_string(new_kernel, "", "Path to the new kernel partition image");
  DEFINE_string(in_file, "",
                "Path to input delta payload file used to hash/sign payloads "
                "and apply delta over old_image (for debugging)");
  DEFINE_string(out_file, "", "Path to output delta payload file");
  DEFINE_string(out_hash_file, "", "Path to output hash file");
  DEFINE_string(out_metadata_hash_file, "",
                "Path to output metadata hash file");
  DEFINE_string(private_key, "", "Path to private key in .pem format");
  DEFINE_string(public_key, "", "Path to public key in .pem format");
  DEFINE_int32(public_key_version,
               chromeos_update_engine::kSignatureMessageCurrentVersion,
               "Key-check version # of client");
  DEFINE_string(prefs_dir, "/tmp/update_engine_prefs",
                "Preferences directory, used with apply_delta");
  DEFINE_string(signature_size, "",
                "Raw signature size used for hash calculation. "
                "You may pass in multiple sizes by colon separating them. E.g. "
                "2048:2048:4096 will assume 3 signatures, the first two with "
                "2048 size and the last 4096.");
  DEFINE_string(signature_file, "",
                "Raw signature file to sign payload with. To pass multiple "
                "signatures, use a single argument with a colon between paths, "
                "e.g. /path/to/sig:/path/to/next:/path/to/last_sig . Each "
                "signature will be assigned a client version, starting from "
                "kSignatureOriginalVersion.");
  DEFINE_int32(chunk_size, -1, "Payload chunk size (-1 -- no limit/default)");
  DEFINE_int64(rootfs_partition_size,
               chromeos_update_engine::kRootFSPartitionSize,
               "RootFS partition size for the image once installed");
  DEFINE_int32(minor_version, 0,
               "The minor version of the payload being generated");

  DEFINE_string(old_channel, "",
                "The channel for the old image. 'dev-channel', 'npo-channel', "
                "etc. Ignored, except during delta generation.");
  DEFINE_string(old_board, "",
                "The board for the old image. 'x86-mario', 'lumpy', "
                "etc. Ignored, except during delta generation.");
  DEFINE_string(old_version, "",
                "The build version of the old image. 1.2.3, etc.");
  DEFINE_string(old_key, "",
                "The key used to sign the old image. 'premp', 'mp', 'mp-v3',"
                " etc");
  DEFINE_string(old_build_channel, "",
                "The channel for the build of the old image. 'dev-channel', "
                "etc, but will never contain special channels such as "
                "'npo-channel'. Ignored, except during delta generation.");
  DEFINE_string(old_build_version, "",
                "The version of the build containing the old image.");

  DEFINE_string(new_channel, "",
                "The channel for the new image. 'dev-channel', 'npo-channel', "
                "etc. Ignored, except during delta generation.");
  DEFINE_string(new_board, "",
                "The board for the new image. 'x86-mario', 'lumpy', "
                "etc. Ignored, except during delta generation.");
  DEFINE_string(new_version, "",
                "The build version of the new image. 1.2.3, etc.");
  DEFINE_string(new_key, "",
                "The key used to sign the new image. 'premp', 'mp', 'mp-v3',"
                " etc");
  DEFINE_string(new_build_channel, "",
                "The channel for the build of the new image. 'dev-channel', "
                "etc, but will never contain special channels such as "
                "'npo-channel'. Ignored, except during delta generation.");
  DEFINE_string(new_build_version, "",
                "The version of the build containing the new image.");

  chromeos::FlagHelper::Init(argc, argv,
      "Generates a payload to provide to ChromeOS' update_engine.\n\n"
      "This tool can create full payloads and also delta payloads if the src\n"
      "image is provided. It also provides debugging options to apply, sign\n"
      "and verify payloads.");
  Terminator::Init();
  Subprocess::Init();

  logging::LoggingSettings log_settings;
  log_settings.log_file     = "delta_generator.log";
  log_settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  log_settings.lock_log     = logging::DONT_LOCK_LOG_FILE;
  log_settings.delete_old   = logging::APPEND_TO_OLD_LOG_FILE;

  logging::InitLogging(log_settings);

  vector<int> signature_sizes;
  ParseSignatureSizes(FLAGS_signature_size, &signature_sizes);

  if (!FLAGS_out_hash_file.empty() || !FLAGS_out_metadata_hash_file.empty()) {
    if (!FLAGS_out_hash_file.empty()) {
      CalculatePayloadHashForSigning(signature_sizes, FLAGS_out_hash_file,
                                     FLAGS_in_file);
    }
    if (!FLAGS_out_metadata_hash_file.empty()) {
      CalculateMetadataHashForSigning(signature_sizes,
                                      FLAGS_out_metadata_hash_file,
                                      FLAGS_in_file);
    }
    return 0;
  }
  if (!FLAGS_signature_file.empty()) {
    SignPayload(FLAGS_in_file, FLAGS_out_file, FLAGS_signature_file);
    return 0;
  }
  if (!FLAGS_public_key.empty()) {
    VerifySignedPayload(FLAGS_in_file, FLAGS_public_key,
                        FLAGS_public_key_version);
    return 0;
  }
  if (!FLAGS_in_file.empty()) {
    ApplyDelta(FLAGS_in_file, FLAGS_old_kernel, FLAGS_old_image,
               FLAGS_prefs_dir);
    return 0;
  }
  CHECK(!FLAGS_new_image.empty());
  CHECK(!FLAGS_out_file.empty());
  CHECK(!FLAGS_new_kernel.empty());

  bool is_delta = !FLAGS_old_image.empty();

  ImageInfo old_image_info;
  ImageInfo new_image_info;

  // Ignore failures. These are optional arguments.
  ParseImageInfo(FLAGS_new_channel,
                 FLAGS_new_board,
                 FLAGS_new_version,
                 FLAGS_new_key,
                 FLAGS_new_build_channel,
                 FLAGS_new_build_version,
                 &new_image_info);

  // Ignore failures. These are optional arguments.
  ParseImageInfo(FLAGS_old_channel,
                 FLAGS_old_board,
                 FLAGS_old_version,
                 FLAGS_old_key,
                 FLAGS_old_build_channel,
                 FLAGS_old_build_version,
                 &old_image_info);

  if (is_delta) {
    LOG(INFO) << "Generating delta update";
    CHECK(!FLAGS_old_dir.empty());
    CHECK(!FLAGS_new_dir.empty());
    if (!utils::IsDir(FLAGS_old_dir.c_str()) ||
        !utils::IsDir(FLAGS_new_dir.c_str())) {
      LOG(FATAL) << "old_dir or new_dir not directory";
    }
  } else {
    LOG(INFO) << "Generating full update";
  }

  // Look for the minor version in the old image if it was not given as an
  // argument.
  if (!CommandLine::ForCurrentProcess()->HasSwitch("minor_version")) {
    uint32_t minor_version;
    base::FilePath image_path(FLAGS_old_dir);
    base::FilePath conf_loc("etc/update_engine.conf");
    base::FilePath conf_path = image_path.Append(conf_loc);
    if (utils::GetMinorVersion(conf_path, &minor_version)) {
      FLAGS_minor_version = minor_version;
    } else {
      if (is_delta) {
        FLAGS_minor_version = kInPlaceMinorPayloadVersion;
      } else {
        FLAGS_minor_version = DeltaPerformer::kFullPayloadMinorVersion;
      }
    }
  }

  if (static_cast<uint32_t>(FLAGS_minor_version) ==
      kInPlaceMinorPayloadVersion ||
      static_cast<uint32_t>(FLAGS_minor_version) ==
      DeltaPerformer::kFullPayloadMinorVersion) {
    uint64_t metadata_size;
    if (!DeltaDiffGenerator::GenerateDeltaUpdateFile(
        FLAGS_old_dir,
        FLAGS_old_image,
        FLAGS_new_dir,
        FLAGS_new_image,
        FLAGS_old_kernel,
        FLAGS_new_kernel,
        FLAGS_out_file,
        FLAGS_private_key,
        FLAGS_chunk_size,
        FLAGS_rootfs_partition_size,
        FLAGS_minor_version,
        is_delta ? &old_image_info : nullptr,
        &new_image_info,
        &metadata_size)) {
      return 1;
    }
  } else {
    LOG(FATAL) << "Unsupported minor payload version: " << FLAGS_minor_version;
  }

  return 0;
}

}  // namespace

}  // namespace chromeos_update_engine

int main(int argc, char** argv) {
  return chromeos_update_engine::Main(argc, argv);
}
