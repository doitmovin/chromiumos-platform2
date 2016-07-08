// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/dso.h"

#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <vector>

#include "base/logging.h"
#include "chromiumos-wide-profiling/compat/string.h"
#include "chromiumos-wide-profiling/file_reader.h"

namespace quipper {

namespace {

// Find a section with a name matching one in |names|. Prefer sections matching
// names earlier in the vector.
Elf_Scn *FindElfSection(Elf *elf, const std::vector<string>& names) {
  size_t shstrndx;  // section index of the section names string table.
  if (elf_getshdrstrndx(elf, &shstrndx) != 0) {
    LOG(ERROR) << "elf_getshdrstrndx" << elf_errmsg(-1);
    return nullptr;
  }
  // Ensure the section header string table is available
  if (!elf_rawdata(elf_getscn(elf, shstrndx), nullptr))
    return nullptr;

  auto best_match = names.end();
  Elf_Scn *best_sec = nullptr;
  Elf_Scn *sec = nullptr;
  while ((sec = elf_nextscn(elf, sec)) != nullptr) {
    GElf_Shdr shdr;
    gelf_getshdr(sec, &shdr);
    char * n = elf_strptr(elf, shstrndx, shdr.sh_name);
    if (!n) {
      LOG(ERROR) << "Couldn't get string: " << shdr.sh_name << " " << shstrndx;
      return nullptr;
    }
    const string name(n);
    auto found = std::find(names.begin(), names.end(), name);
    if (found < best_match) {
      if (found == names.begin())
        return sec;
      best_sec = sec;
      best_match = found;
    }
  }

  return best_sec;
}

bool GetBuildID(Elf *elf, string* buildid) {
  Elf_Kind kind = elf_kind(elf);
  if (kind != ELF_K_ELF) {
    DLOG(ERROR) << "Not an ELF file: " << elf_errmsg(-1);
    return false;
  }

  static const std::vector<string> kNoteSectionNames{
      ".note.gnu.build-id", ".notes", ".note"};
  Elf_Scn *section = FindElfSection(elf, kNoteSectionNames);
  if (!section) {
    DLOG(ERROR) << "No note section found";
    return false;
  }

  Elf_Data *data = elf_getdata(section, nullptr);
  if (data == nullptr)
    return false;

  char *buf = reinterpret_cast<char*>(data->d_buf);
  GElf_Nhdr note_header;
  size_t name_off;
  size_t desc_off;
  for (size_t off = 0, next = 0;
      (next = gelf_getnote(data, off, &note_header, &name_off, &desc_off)) > 0;
       off = next) {
    // name is null-padded to a 4-byte boundary.
    string name(buf + name_off, strnlen(buf + name_off, note_header.n_namesz));
    string desc(buf + desc_off, note_header.n_descsz);
    if (note_header.n_type == NT_GNU_BUILD_ID && name == ELF_NOTE_GNU) {
      *buildid = desc;
      return true;
    }
  }

  return false;
}

}  // namespace

void InitializeLibelf() {
  const unsigned int kElfVersionNone = EV_NONE;  // correctly typed.
  CHECK_NE(kElfVersionNone, elf_version(EV_CURRENT)) << elf_errmsg(-1);
}

bool ReadElfBuildId(string filename, string* buildid) {
  InitializeLibelf();
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    LOG(ERROR) << "Failed to open ELF file: " << filename;
    return false;
  }

  Elf *elf = elf_begin(fd, ELF_C_READ_MMAP, nullptr);
  if (elf == nullptr) {
    LOG(ERROR) << "Could not read ELF file: " << filename;
    close(fd);
    return false;
  }

  bool err = GetBuildID(elf, buildid);

  elf_end(elf);
  close(fd);

  return err;
}

// read /sys/module/<module_name>/notes/.note.gnu.build-id
bool ReadModuleBuildId(string module_name, string* buildid) {
  string note_filename =
      "/sys/module/" + module_name + "/notes/.note.gnu.build-id";

  FileReader file(note_filename);
  if (!file.IsOpen())
    return false;

  return ReadBuildIdNote(&file, buildid);
}

bool ReadBuildIdNote(DataReader* data, string* buildid) {
  GElf_Nhdr note_header;

  while (data->ReadData(sizeof(note_header), &note_header)) {
    size_t name_size = Align<4>(note_header.n_namesz);
    size_t desc_size = Align<4>(note_header.n_descsz);

    string name;
    if (!data->ReadString(name_size, &name))
      return false;
    string desc;
    if (!data->ReadDataString(desc_size, &desc))
      return false;
    if (note_header.n_type == NT_GNU_BUILD_ID && name == ELF_NOTE_GNU) {
      *buildid = desc;
      return true;
    }
  }
  return false;
}

bool IsKernelNonModuleName(string name) {
  // List from kernel: tools/perf/util/dso.c : __kmod_path__parse()
  static const std::vector<string> kKernelNonModuleNames {
    "[kernel.kallsyms]",
    "[guest.kernel.kallsyms",
    "[vdso]",
    "[vsyscall]",
  };

  for (const auto &n : kKernelNonModuleNames) {
    if (name.compare(0, n.size(), n) == 0)
      return true;
  }
  return false;
}

}  // namespace quipper