#!/bin/bash
# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

OUT=$1
v=$2
DIR=$(cd -P -- "$(dirname -- "$0")" && pwd -P)

deps=$(<"${OUT}"/gen/libweave-${v}-deps.txt)
sed \
  -e "s/@BSLOT@/${v}/g" \
  -e "s/@PRIVATE_PC@/${deps}/g" \
  "${DIR}/libweave.pc.in" > "${OUT}/lib/libweave-${v}.pc"


deps_test=$(<"${OUT}"/gen/libweave-test-${v}-deps.txt)
sed \
  -e "s/@BSLOT@/${v}/g" \
  -e "s/@PRIVATE_PC@/${deps_test}/g" \
  "${DIR}/libweave-test.pc.in" > "${OUT}/lib/libweave-test-${v}.pc"
