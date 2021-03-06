// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Plugin tests link against this file, but not against the rest of
// cromo.  Therefore this file should not have dependencies on the
// rest of cromo.

#include "cromo/utilities.h"

#include <stdio.h>
#include <stdlib.h>

#include <iomanip>
#include <sstream>
#include <utility>

#include <base/logging.h>

namespace utilities {

const char* ExtractString(const DBusPropertyMap properties,
                          const char* key,
                          const char* not_found_response,
                          DBus::Error& error) {  // NOLINT - refs.
  DBusPropertyMap::const_iterator p;
  const char* to_return = not_found_response;
  try {
    p = properties.find(key);
    if (p != properties.end()) {
      to_return = p->second.reader().get_string();
    }
  } catch (const DBus::Error& e) {
    LOG(ERROR) << "Bad type for: " << key;
    // Setting an already-set error causes an assert fail inside dbus-c++.
    if (!error.is_set()) {
    // NB: the copy constructor for DBus::Error causes a template to
    // be instantiated that kills our -Werror build
      error.set(e.name(), e.message());
    }
  }
  return to_return;
}

uint32_t ExtractUint32(const DBusPropertyMap properties,
                       const char* key,
                       uint32_t not_found_response,
                       DBus::Error& error) {  // NOLINT - refs.
  DBusPropertyMap::const_iterator p;
  unsigned int to_return = not_found_response;
  try {
    p = properties.find(key);
    if (p != properties.end()) {
      to_return = p->second.reader().get_uint32();
    }
  } catch (const DBus::Error& e) {
    LOG(ERROR) << "Bad type for: " << key;
    // Setting an already-set error causes an assert fail inside dbus-c++.
    if (!error.is_set()) {
    // NB: the copy constructor for DBus::Error causes a template to
    // be instantiated that kills our -Werror build
      error.set(e.name(), e.message());
    }
  }
  return to_return;
}

bool HexEsnToDecimal(const std::string& esn_hex, std::string* out) {
  size_t length = esn_hex.length();
  if (length > 8) {
    LOG(ERROR) << "Long ESN: " << esn_hex;
    return false;
  }
  errno = 0;
  const char* start = esn_hex.c_str();
  char* end;
  uint32_t esn = strtoul(start, &end, 16);
  if (errno != 0 || *end != '\0') {
    LOG(ERROR) << "Bad ESN: " << esn_hex;
    return false;
  }
  uint32_t mfr = (esn >> 24) & 0xff;
  uint32_t serial = esn & 0x00ffffff;

  // Decimal ESN is 11 chars
  char decimal[12];
  memset(decimal, '\0', sizeof(decimal));
  int rc = snprintf(decimal, sizeof(decimal), "%03d%08d", mfr, serial);
  if (rc != 11) {
    LOG(ERROR) << "Format failure";
    return false;
  }
  if (out) {
    *out = decimal;
  }
  return true;
}

#define C_(c) {c}
#define C2(c) {0xc2, c}
#define C3(c) {0xc3, c}
#define CE(c) {0xce, c}

static uint8_t Gsm7ToUtf8Map[][3] = {
C_('@'),  C2(0xa3), C_('$'),  C2(0xa5), C3(0xa8), C3(0xa9), C3(0xb9), C3(0xac),
C3(0xb2), C3(0x87), C_('\n'), C3(0x98), C3(0xb8), C_('\r'), C3(0x85), C3(0xa5),
CE(0x94), C_('_'),  CE(0xa6), CE(0x93), CE(0x9b), CE(0xa9), CE(0xa0), CE(0xa8),
CE(0xa3), CE(0x98), CE(0x9e), C_(' '),  C3(0x86), C3(0xa6), C3(0x9f), C3(0x89),
C_(' '),  C_('!'),  C_('"'),  C_('#'),  C2(0xa4), C_('%'),  C_('&'),  C_('\''),
C_('('),  C_(')'),  C_('*'),  C_('+'),  C_(','),  C_('-'),  C_('.'),  C_('/'),
C_('0'),  C_('1'),  C_('2'),  C_('3'),  C_('4'),  C_('5'),  C_('6'),  C_('7'),
C_('8'),  C_('9'),  C_(':'),  C_(';'),  C_('<'),  C_('='),  C_('>'),  C_('?'),
C2(0xa1), C_('A'),  C_('B'),  C_('C'),  C_('D'),  C_('E'),  C_('F'),  C_('G'),
C_('H'),  C_('I'),  C_('J'),  C_('K'),  C_('L'),  C_('M'),  C_('N'),  C_('O'),
C_('P'),  C_('Q'),  C_('R'),  C_('S'),  C_('T'),  C_('U'),  C_('V'),  C_('W'),
C_('X'),  C_('Y'),  C_('Z'),  C3(0x84), C3(0x96), C3(0x91), C3(0x9c), C2(0xa7),
C2(0xbf), C_('a'),  C_('b'),  C_('c'),  C_('d'),  C_('e'),  C_('f'),  C_('g'),
C_('h'),  C_('i'),  C_('j'),  C_('k'),  C_('l'),  C_('m'),  C_('n'),  C_('o'),
C_('p'),  C_('q'),  C_('r'),  C_('s'),  C_('t'),  C_('u'),  C_('v'),  C_('w'),
C_('x'),  C_('y'),  C_('z'),  C3(0xa4), C3(0xb6), C3(0xb1), C3(0xbc), C3(0xa0)
};

// 2nd dimension == 5 ensures that all the sub-arrays end with a 0 byte
static uint8_t ExtGsm7ToUtf8Map[][5] = {
  {0x0a, 0xc},
  {0x14, '^'},
  {0x28, '{'},
  {0x29, '}'},
  {0x2f, '\\'},
  {0x3c, '['},
  {0x3d, '~'},
  {0x3e, ']'},
  {0x40, '|'},
  {0x65, 0xe2, 0x82, 0xac}
};

static std::map<std::pair<uint8_t, uint8_t>, char> Utf8ToGsm7Map;

/*
 * Convert a packed GSM7 encoded string to UTF-8 by first unpacking
 * the string into septets, then mapping each character to its UTF-8
 * equivalent.
 */
std::string Gsm7ToUtf8String(const uint8_t* gsm7,
                             size_t gsm7_length,
                             size_t num_septets,
                             uint8_t bit_offset) {
  std::vector<uint8_t> septets(num_septets);
  uint8_t saved_bits = 0;
  size_t written = 0;
  uint8_t* const septets_start = &septets[0];
  uint8_t* cp = septets_start;
  size_t i = 0;

  // unpack
  while (written < num_septets) {
    for (int j = 0; written < num_septets && j < 7; j++) {
      uint8_t octet;
      if (bit_offset == 0) {
        octet = gsm7[i];
      } else {
        octet = (gsm7[i] >> bit_offset);
        // The following check prevents the code from accessing beyond the array
        // boundary of |gsm7|. This is ok because the |mask| applied to |octet|
        // will discard the bits that are supposed to be taken from gsm7[i + 1],
        // which means there is no need to get bits from gsm7[i + 1] at all.
        if (i + 1 < gsm7_length) {
          octet |= (gsm7[i + 1] << (8 - bit_offset));
        }
      }
      uint8_t mask = 0xff >> (j + 1);
      uint8_t c = ((octet & mask) << j) | saved_bits;
      *cp++ = c;
      ++written;
      saved_bits = (octet & ~mask) >> (7 - j);
      ++i;
    }
    if (written < num_septets) {
      *cp++ = saved_bits;
      ++written;
    }
    saved_bits = 0;
  }
  size_t nseptets = cp - septets_start;
  cp = septets_start;

  // now map the septets into their corresponding UTF-8 characters
  std::string str;
  for (i = 0; i < nseptets; ++i, ++cp) {
    uint8_t* mp = nullptr;
    if (*cp == 0x1b) {
      ++cp;
      for (int k = 0; k < 10; k++) {
        mp = &ExtGsm7ToUtf8Map[k][0];
        if (*mp == *cp) {
          ++mp;
          ++i;
          break;
        }
      }
    } else {
      mp = &Gsm7ToUtf8Map[*cp][0];
    }
    if (mp) {
      while (*mp != '\0')
        str += *mp++;
    }
  }

  return str;
}

static void InitializeUtf8ToGsm7Map() {
  for (int i = 0; i < 128; i++) {
    Utf8ToGsm7Map[std::pair<uint8_t, uint8_t>(Gsm7ToUtf8Map[i][0],
                                              Gsm7ToUtf8Map[i][1])] = i;
  }
}

std::vector<uint8_t> Utf8StringToGsm7(const std::string& input) {
  if (Utf8ToGsm7Map.size() == 0)
    InitializeUtf8ToGsm7Map();

  std::vector<uint8_t> septets;
  size_t length = input.length();
  size_t num_septets = 0;

  // First map each UTF-8 character to its GSM7 equivalent.
  for (size_t i = 0; i < length; i++) {
    std::pair<uint8_t, uint8_t> chpair;
    char ch = input.at(i);
    uint8_t thirdch = 0xff;
    // Check whether this is a one byte UTF-8 sequence, or the
    // start of a two or three byte sequence.
    if ((ch & 0x80) == 0) {
      chpair.first = ch;
      chpair.second = 0;
    } else if ((ch & 0xe0) == 0xc0) {
      chpair.first = ch;
      chpair.second = input.at(++i);
    } else if ((ch & 0xf0) == 0xe0) {
      chpair.first = ch;
      chpair.second = input.at(++i);
      thirdch = input.at(++i);
    }
    std::map<std::pair<uint8_t, uint8_t>, char>::iterator it;
    it = Utf8ToGsm7Map.find(chpair);
    if (it != Utf8ToGsm7Map.end()) {
      septets.push_back(it->second);
    } else {
      // not found in the map. search the list of extended characters,
      // but first handle one special case for the 3-byte Euro sign
      if (chpair.first == 0xe2 && chpair.second == 0x82 && thirdch == 0xac) {
        septets.push_back(0x1b);
        septets.push_back(0x65);
      } else if (chpair.second == 0) {
        for (int j = 0; j < 9; ++j) {
          if (ExtGsm7ToUtf8Map[j][1] == chpair.first) {
            septets.push_back(0x1b);
            septets.push_back(ExtGsm7ToUtf8Map[j][0]);
          }
        }
      }
    }
    // If character wasn't mapped successfully, insert a space
    if (septets.size() == num_septets)
      septets.push_back(' ');
    num_septets = septets.size();
  }

  // Now pack the septets into octets. The
  // first byte gives the number of septets.
  std::vector<uint8_t> octets;

  octets.push_back(num_septets);
  int shift = 0;
  for (size_t k = 0; k < num_septets; ++k) {
    uint8_t septet = septets.at(k);
    uint8_t octet;
    if (shift != 7) {
      octet = (septet >> shift);
      if (k < num_septets - 1)
        octet |= septets.at(k+1) << (7-shift);
      octets.push_back(octet);
    }
    if (++shift == 8)
      shift = 0;
  }

  return octets;
}

std::string Ucs2ToUtf8String(const uint8_t* ucs2, size_t length) {
  std::string str;
  uint8_t num_chars = length >> 1;

  for (int i = 0; i < num_chars; ++i) {
    uint16_t ucs2char = ucs2[0] << 8 | ucs2[1];
    if (ucs2char <= 0x7f) {
      str += ucs2[1];
    } else if (ucs2char <= 0x7ff) {
      str += (uint8_t)(0xc0 | ((ucs2char & 0x7c0) >> 6));
      str += (uint8_t)(0x80 | (ucs2char & 0x3f));
    } else {
      str += (uint8_t)(0xe0 | ((ucs2char & 0xf000) >> 12));
      str += (uint8_t)(0x80 | ((ucs2char & 0xfc0) >> 6));
      str += (uint8_t)(0x80 | (ucs2char & 0x3f));
    }
    ucs2 += 2;
  }
  return str;
}

std::vector<uint8_t> Utf8StringToUcs2(const std::string& input) {
  std::vector<uint8_t> octets;
  size_t length = input.length();

  // First byte gives the length in octets of the UCS-2 string
  // Insert a placeholder value until we know the true length.
  octets.push_back(0);
  for (size_t i = 0; i < length; i++) {
    char char1 = input.at(i);
    // Check whether this is a one byte UTF-8 sequence, or the
    // start of a two or three byte sequence.
    if ((char1 & 0x80) == 0) {
      octets.push_back(0);
      octets.push_back(char1);
    } else if ((char1 & 0xe0) == 0xc0) {
      uint8_t char2 = input.at(++i);
      octets.push_back((char1 >> 2) & 0x7);
      octets.push_back(((char1 & 0x3) << 6) | (char2 & 0x3f));
    } else if ((char1 & 0xf0) == 0xe0) {
      uint8_t char2 = input.at(++i);
      uint8_t char3 = input.at(++i);
      octets.push_back(((char1 & 0xf) << 4) | ((char2 & 0x30) >> 2));
      octets.push_back(((char2 & 0x3) << 6) | (char3 & 0x3f));
    } else {
      // character not representable in UCS-2, insert a space
      octets.push_back(0);
      octets.push_back(' ');
    }
  }
  octets[0] = octets.size() - 1;
  return octets;
}

void DumpHex(const uint8_t* buf, size_t size) {
  size_t nlines = (size+15) / 16;
  size_t limit;

  for (size_t i = 0; i < nlines; i++) {
    std::ostringstream ostr;
    ostr << std::hex;
    ostr.fill('0');
    ostr.width(8);
    if (i*16 + 16 >= size)
      limit = size - i*16;
    else
      limit = 16;
    ostr << i*16 << "  ";
    ostr.fill('0');
    ostr.width(2);
    for (size_t j = 0; j < limit; j++) {
      uint8_t byte = buf[i*16+j];
      ostr << std::setw(0) << " " << std::setw(2) << std::setfill('0')
           << static_cast<unsigned int>(byte);
    }
    LOG(INFO) << ostr.str();
  }
}

}  // namespace utilities
