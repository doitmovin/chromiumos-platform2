// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Command line tool to parse data. Data is read from stdin as string or
// protobuf and returned through stdout in string or protobuf format. The tool
// is invoked by the authpolicy daemon in a secure sandbox. It is done this way
// since parsing the output is considered insecure.
//
// Usage:
//   authpolicy_parser <command>
//   For a list of commands see constants.h.
//   Each command reads additional arguments from stdin. See code for details.
//
// Logs to syslog.

#include <time.h>

#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/syslog_logging.h>

#include "authpolicy/constants.h"
#include "authpolicy/log_level.h"
#include "authpolicy/platform_helper.h"
#include "authpolicy/policy/preg_policy_encoder.h"
#include "authpolicy/samba_helper.h"
#include "bindings/authpolicy_containers.pb.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/cloud_policy.pb.h"

namespace em = enterprise_management;

namespace authpolicy {

namespace {

// 'net ads gpo list'' tokens.
const char kGpoToken_Separator[] = "---------------------";
const char kGpoToken_Name[] = "name";
const char kGpoToken_Filesyspath[] = "filesyspath";
const char kGpoToken_VersionUser[] = "version_user";
const char kGpoToken_VersionMachine[] = "version_machine";
const char kGpoToken_Options[] = "options";

// Length of the klist date/time format (mm/dd/yy HH:MM:SS).
const int kDateTimeStringLength = 18;

// Various offsets from the beginning of a line of date/time strings in the
// klist output.
const size_t kValidFromOffset = 0;
const size_t kExpiresOffset = 19;
const size_t kRenewUntilOffset = 11;

// String in klist output that prefixes the renewal lifetime.
const char kRenewUntil[] = "renew until ";

struct GpoEntry {
  GpoEntry() { Clear(); }

  void Clear() {
    name.clear();
    filesyspath.clear();
    version_user = 0;
    version_machine = 0;
    gp_flags = kGpFlagInvalid;
  }

  bool IsValid() const {
    return !name.empty() && !filesyspath.empty() &&
           !(version_user == 0 && version_machine == 0) &&
           gp_flags != kGpFlagInvalid;
  }

  bool IsEmpty() const {
    return name.empty() && filesyspath.empty() && version_user == 0 &&
           version_machine == 0 && gp_flags == kGpFlagInvalid;
  }

  void Log() const {
    LOG_IF(INFO, kLogGpo) << "  Name:        " << name;
    LOG_IF(INFO, kLogGpo) << "  Filesyspath: " << filesyspath;
    LOG_IF(INFO, kLogGpo) << "  Version:     " << version_user << " (user) "
                          << version_machine << " (machine)";
    LOG_IF(INFO, kLogGpo) << "  GPFLags:     " << gp_flags;
  }

  std::string name;
  std::string filesyspath;
  unsigned int version_user;
  unsigned int version_machine;
  int gp_flags;
};

void PushGpo(const GpoEntry& gpo,
             PolicyScope scope,
             std::vector<GpoEntry>* gpo_list) {
  if (gpo.IsEmpty())
    return;

  if (!gpo.IsValid()) {
    LOG_IF(INFO, kLogGpo) << "Ignoring invalid GPO";
    gpo.Log();
    return;
  }

  // Filter out GPOs we don't need. If version_user == 0, there's no user
  // policy stored in that GPO. Similarly, if version_machine == 0, there's no
  // device policy.
  const char* filter_reason = nullptr;
  switch (scope) {
    case PolicyScope::USER:
      if (gpo.version_user == 0)
        filter_reason = "user version is 0";
      else if (gpo.gp_flags & kGpFlagUserDisabled)
        filter_reason = "user disabled flag is set";
      break;
    case PolicyScope::MACHINE:
      if (gpo.version_machine == 0)
        filter_reason = "machine version is 0";
      else if (gpo.gp_flags & kGpFlagMachineDisabled)
        filter_reason = "machine disabled flag is set";
      break;
  }
  if (filter_reason) {
    LOG_IF(INFO, kLogGpo) << "Filtered out GPO (" << filter_reason << ")";
    gpo.Log();
  } else {
    gpo_list->push_back(gpo);
  }
}

// Prints |str| to stdout for the caller of this tool. Returns an exit code that
// indicates success or failure.
int OutputForCaller(const std::string& str) {
  if (!base::WriteFileDescriptor(STDOUT_FILENO, str.c_str(), str.size())) {
    LOG(ERROR) << "Failed to write output for caller";
    return EXIT_CODE_WRITE_OUTPUT_FAILED;
  }
  return EXIT_CODE_OK;
}

// Parses the substring starting at offset |offset| of |str| for a date/time
// formatted mm/dd/yy HH:MM:SS. The time is interpreted as local time. Sets
// |time| to the number of seconds in the epoch or 0 on error. Returns true on
// success.
bool ParseTgtDateTime(const std::string& str, size_t offset, time_t* time) {
  *time = 0;
  if (offset >= str.size())
    return false;

  std::string datetime = str.substr(offset, kDateTimeStringLength);
  if (datetime.size() < kDateTimeStringLength)
    return false;

  struct tm tm = {};
  if (!strptime(datetime.c_str(), "%m/%d/%y %H:%M:%S", &tm))
    return false;

  // Figure out daylight saving time (strptime doesn't set this).
  tm.tm_isdst = -1;

  *time = mktime(&tm);
  return true;
}

// Parses the output of net ads search to get the user's objectGUID and prints
// it to stdout.
int ParseAccountInfo(const std::string& net_out) {
  std::string object_guid, sam_account_name, display_name, given_name;
  if (!FindToken(net_out, ':', kSearchObjectGUID, &object_guid) ||
      !FindToken(net_out, ':', kSearchSAMAccountName, &sam_account_name)) {
    LOG(ERROR) << "Failed to parse account info";
    return EXIT_CODE_FIND_TOKEN_FAILED;
  }
  // Output data as proto blob.
  protos::AccountInfo account_info;
  account_info.set_object_guid(object_guid);
  account_info.set_sam_account_name(sam_account_name);

  // Attributes 'displayName' and 'givenName' are optional. May be missing for
  // accounts like 'Administrator' or for partially set up accounts.
  if (FindToken(net_out, ':', kSearchDisplayName, &display_name))
    account_info.set_display_name(display_name);
  if (FindToken(net_out, ':', kSearchGivenName, &given_name))
    account_info.set_given_name(given_name);

  std::string account_info_blob;
  if (!account_info.SerializeToString(&account_info_blob)) {
    LOG(ERROR) << "Failed to convert account info proto to string";
    return EXIT_CODE_WRITE_OUTPUT_FAILED;
  }
  return OutputForCaller(account_info_blob);
}

// Parses the output of net ads info to get the domain controller name and KDC
// IP address. Prints it to stdout.
int ParseRealmInfo(const std::string& net_out) {
  // Parse output for dc_name in 'LDAP server name: dc_name.some.domain'.
  std::string dc_name, kdc_ip;
  if (!FindToken(net_out, ':', "LDAP server name", &dc_name) ||
      !FindToken(net_out, ':', "KDC server", &kdc_ip)) {
    return EXIT_CODE_FIND_TOKEN_FAILED;
  }

  // We're only interested in the part before the dot.
  size_t dot_pos = dc_name.find('.');
  dc_name = dc_name.substr(0, dot_pos);

  protos::RealmInfo realm_info;
  realm_info.set_dc_name(dc_name);
  realm_info.set_kdc_ip(kdc_ip);
  std::string realm_info_blob;
  if (!realm_info.SerializeToString(&realm_info_blob)) {
    LOG(ERROR) << "Failed to convert realm info proto to string";
    return EXIT_CODE_WRITE_OUTPUT_FAILED;
  }

  return OutputForCaller(realm_info_blob);
}

// Parses the output of net ads workgroup to get the workgroup and prints it to
// stdout.
int ParseWorkgroup(const std::string& net_out) {
  std::string workgroup;
  if (!FindToken(net_out, ':', "Workgroup", &workgroup))
    return EXIT_CODE_FIND_TOKEN_FAILED;

  return OutputForCaller(workgroup);
}

// Parses the output of net ads gpo list to get the list of GPOs. Prints out a
// serialized GpoList blob to stdout.
int ParseGpoList(const std::string& net_out, PolicyScope scope) {
  // Parse net output.
  GpoEntry current_gpo;
  std::vector<GpoEntry> gpo_list;
  std::vector<std::string> lines = base::SplitString(
      net_out, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  LOG_IF(INFO, kLogGpo) << "Parsing GPO list (" << lines.size() << " lines)";
  bool found_separator = false;
  for (const std::string& line : lines) {
    if (line.find(kGpoToken_Separator) == 0) {
      // Separator between entries. Process last gpo if any.
      PushGpo(current_gpo, scope, &gpo_list);
      current_gpo.Clear();
      found_separator = true;
      continue;
    }

    // Collect data
    const size_t colon_pos = line.find(":");
    if (colon_pos == std::string::npos || colon_pos + 1 >= line.size())
      continue;
    const std::string key = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);
    base::TrimWhitespaceASCII(value, base::TRIM_ALL, &value);

    bool already_set = false;
    bool version_error = false;
    bool flags_error = false;
    if (key == kGpoToken_Name) {
      already_set = !current_gpo.name.empty();
      current_gpo.name = value;
    } else if (key == kGpoToken_Filesyspath) {
      already_set = !current_gpo.filesyspath.empty();
      current_gpo.filesyspath = value;
    } else if (key == kGpoToken_VersionUser) {
      already_set = current_gpo.version_user != 0;
      version_error = !ParseGpoVersion(value, &current_gpo.version_user);
    } else if (key == kGpoToken_VersionMachine) {
      already_set = current_gpo.version_machine != 0;
      version_error = !ParseGpoVersion(value, &current_gpo.version_machine);
    } else if (key == kGpoToken_Options) {
      already_set = current_gpo.gp_flags != kGpFlagInvalid;
      flags_error = !ParseGpFlags(value, &current_gpo.gp_flags);
    }

    // Sanity check that we don't miss separators between GPOs.
    if (already_set) {
      LOG(ERROR) << "Failed to parse GPO data (bad format)";
      return EXIT_CODE_PARSE_INPUT_FAILED;
    }

    if (version_error) {
      LOG(ERROR) << "Failed to parse GPO version '" << value << "'";
      return EXIT_CODE_PARSE_INPUT_FAILED;
    }

    if (flags_error) {
      LOG(ERROR) << "Failed to parse GP flags '" << value << "'";
      return EXIT_CODE_PARSE_INPUT_FAILED;
    }
  }

  // Just in case there's no separator in the end.
  PushGpo(current_gpo, scope, &gpo_list);

  if (!found_separator) {
    // This usually happens when something went wrong, e.g. connection error.
    LOG(ERROR) << "Failed to parse GPO data (no separator, did net fail?)";
    LOG(ERROR) << "Net response: " << net_out;
    return EXIT_CODE_PARSE_INPUT_FAILED;
  }

  if (authpolicy::kLogGpo && LOG_IS_ON(INFO)) {
    LOG(INFO) << "Found " << gpo_list.size() << " GPOs.";
    for (size_t n = 0; n < gpo_list.size(); ++n) {
      LOG(INFO) << n + 1 << ")";
      gpo_list[n].Log();
    }
  }

  // Convert to proto.
  protos::GpoList gpo_list_proto;
  for (const GpoEntry& gpo : gpo_list) {
    // Split the filesyspath, e.g.
    //   \\chrome.lan\SysVol\chrome.lan\Policies\{3507856D-...-CF144DC5CC3A}
    // into
    // - the base path (chrome.lan/SysVol) and
    // - the directory (chrome.lan\Policies\...).
    // Note the change from \ to / in base path.
    std::vector<std::string> file_parts = base::SplitString(
        gpo.filesyspath, "\\/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    if (file_parts.size() < 4 || !file_parts[0].empty() ||
        !file_parts[1].empty()) {
      LOG(ERROR) << "Failed to split filesyspath '" << gpo.filesyspath
                 << "' into service and directory parts";
      return EXIT_CODE_PARSE_INPUT_FAILED;
    }
    std::string basepath = file_parts[2] + "/" + file_parts[3];
    file_parts =
        std::vector<std::string>(file_parts.begin() + 4, file_parts.end());
    std::string directory = base::JoinString(file_parts, "\\");

    protos::GpoEntry* gpo_proto = gpo_list_proto.add_entries();
    gpo_proto->set_name(gpo.name);
    gpo_proto->set_basepath(basepath);
    gpo_proto->set_directory(directory);
  }

  // Output data as proto blob.
  std::string gpo_list_blob;
  if (!gpo_list_proto.SerializeToString(&gpo_list_blob)) {
    LOG(ERROR) << "Failed to convert GPO list proto to string";
    return EXIT_CODE_WRITE_OUTPUT_FAILED;
  }
  return OutputForCaller(gpo_list_blob);
}

// Parses a set of GPO files and assembles a user or device policy proto. Writes
// the serialized policy blob to stdout. |gpo_file_paths_blob| is expected to be
// a serialized |protos::FilePathList| proto blob.
int ParsePreg(const std::string& gpo_file_paths_blob, PolicyScope scope) {
  // Parse FilePathList proto blob.
  protos::FilePathList gpo_file_paths_proto;
  if (!gpo_file_paths_proto.ParseFromString(gpo_file_paths_blob)) {
    LOG(ERROR) << "Failed to parse file paths blob";
    return EXIT_CODE_READ_INPUT_FAILED;
  }

  // Convert to list of base::FilePaths.
  std::vector<base::FilePath> gpo_file_paths;
  for (int n = 0; n < gpo_file_paths_proto.entries_size(); ++n)
    gpo_file_paths.push_back(base::FilePath(gpo_file_paths_proto.entries(n)));

  std::string policy_blob;
  switch (scope) {
    case PolicyScope::USER: {
      // Parse files into a user policy proto.
      em::CloudPolicySettings policy;
      if (!policy::ParsePRegFilesIntoUserPolicy(gpo_file_paths, &policy))
        return EXIT_CODE_PARSE_INPUT_FAILED;

      // Serialize user policy proto to string.
      if (!policy.SerializeToString(&policy_blob))
        return EXIT_CODE_WRITE_OUTPUT_FAILED;
      break;
    }
    case PolicyScope::MACHINE: {
      // Parse files into a device policy proto.
      em::ChromeDeviceSettingsProto policy;
      if (!policy::ParsePRegFilesIntoDevicePolicy(gpo_file_paths, &policy))
        return EXIT_CODE_PARSE_INPUT_FAILED;

      // Serialize policy proto to string.
      if (!policy.SerializeToString(&policy_blob))
        return EXIT_CODE_WRITE_OUTPUT_FAILED;
      break;
    }
    default: { LOG(FATAL) << "invalid scope"; }
  }

  // Print the serialized policy proto to stdout.
  return OutputForCaller(policy_blob);
}

// Parses the validity and renewal lifetimes of a TGT from the output of klist.
// Writes the serialized lifetime protobuf blob to stdout. For sample klist
// output see stub_klist_main.cc.
int ParseTgtLifetime(const std::string& klist_out) {
  std::vector<std::string> lines = base::SplitString(
      klist_out, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  time_t valid_from, expires, renew_until = 0;
  for (size_t n = 0; n < lines.size(); ++n) {
    if (Contains(lines[n], "krbtgt/") &&
        ParseTgtDateTime(lines[n], kValidFromOffset, &valid_from) &&
        ParseTgtDateTime(lines[n], kExpiresOffset, &expires)) {
      if (n + 1 < lines.size() &&
          base::StartsWith(
              lines[n + 1], kRenewUntil, base::CompareCase::SENSITIVE) &&
          ParseTgtDateTime(lines[n + 1], kRenewUntilOffset, &renew_until)) {
        ++n;
      }

      // If the caller checked klist -s beforehand, the TGT should be valid and
      // these warnings should never be printed.
      time_t now = time(NULL);
      if (now < valid_from) {
        LOG(WARNING) << "TGT not yet valid? (now=" << now
                     << ", valid_from=" << valid_from << ")";
      }
      if (now > expires) {
        LOG(WARNING) << "TGT already expired? (now=" << now
                     << ", expires=" << expires << ")";
      }

      // Output lifetime as protobuf blob.
      protos::TgtLifetime lifetime;
      lifetime.set_validity_seconds(std::max<int64_t>(expires - now, 0));
      lifetime.set_renewal_seconds(std::max<int64_t>(renew_until - now, 0));

      std::string lifetime_blob;
      if (!lifetime.SerializeToString(&lifetime_blob)) {
        LOG(ERROR) << "Failed to convert lifetime proto to string";
        return EXIT_CODE_WRITE_OUTPUT_FAILED;
      }
      return OutputForCaller(lifetime_blob);
    }
  }

  LOG(ERROR) << "Failed to find krbtgt in klist output";
  return EXIT_CODE_PARSE_INPUT_FAILED;
}

int HandleCommand(const std::string& cmd, const std::string& arg) {
  if (cmd == kCmdParseRealmInfo)
    return ParseRealmInfo(arg);
  if (cmd == kCmdParseWorkgroup)
    return ParseWorkgroup(arg);
  if (cmd == kCmdParseAccountInfo)
    return ParseAccountInfo(arg);
  if (cmd == kCmdParseUserGpoList)
    return ParseGpoList(arg, PolicyScope::USER);
  if (cmd == kCmdParseDeviceGpoList)
    return ParseGpoList(arg, PolicyScope::MACHINE);
  if (cmd == kCmdParseUserPreg)
    return ParsePreg(arg, PolicyScope::USER);
  if (cmd == kCmdParseDevicePreg)
    return ParsePreg(arg, PolicyScope::MACHINE);
  if (cmd == kCmdParseTgtLifetime)
    return ParseTgtLifetime(arg);

  LOG(ERROR) << "Bad command '" << cmd << "'";
  return EXIT_CODE_BAD_COMMAND;
}

}  // namespace

}  // namespace authpolicy

int main(int argc, char* argv[]) {
  brillo::OpenLog("authpolicy_parser", true);
  brillo::InitLog(brillo::kLogToSyslog);

  // Required for base::SysInfo.
  base::AtExitManager at_exit_manager;

  // Require one argument, one of the kCmdParse* strings.
  if (argc <= 1) {
    LOG(ERROR) << "No command";
    return authpolicy::EXIT_CODE_BAD_COMMAND;
  }
  const char* cmd = argv[1];

  // All commands take additional arguments via stdin.
  std::string stdin;
  if (!authpolicy::ReadPipeToString(STDIN_FILENO, &stdin)) {
    LOG(ERROR) << "Failed to read stdin";
    return authpolicy::EXIT_CODE_READ_INPUT_FAILED;
  }

  return authpolicy::HandleCommand(cmd, stdin);
}
