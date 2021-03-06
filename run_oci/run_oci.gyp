# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'target_defaults': {
    'variables': {
      'deps': [
        'libchrome-<(libbase_ver)',
      ],
      'enable_exceptions': 1,
    },
  },
  'targets': [
    {
      'target_name': 'container_config_parser',
      'type': 'shared_library',
      'sources': [
        'container_config_parser.cc',
      ],
    },
    {
      'target_name': 'run_oci',
      'type': 'executable',
      'variables': {
        'deps': [
          'libcontainer',
          'libbrillo-<(libbase_ver)',
          'libcrypto',
        ],
      },
      'sources': [
        'container_config_parser.cc',
        'run_oci.cc',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'container_config_parser_unittest',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'defines': ['UNIT_TEST'],
          'variables': {
            'deps': [
              'libbrillo-test-<(libbase_ver)',
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'container_config_parser.cc',
            'container_config_parser_unittest.cc',
          ],
        },
      ]},
    ],
  ],
}
