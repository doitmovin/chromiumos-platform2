// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/nss_util.h"

#include <base/basictypes.h>
#include <base/crypto/rsa_private_key.h>
#include <base/crypto/signature_verifier.h>
#include <base/file_path.h>
#include <base/logging.h>
#include <base/nss_util.h>
#include <base/scoped_ptr.h>
#include <cros/chromeos_login.h>
#include <glib/garray.h>

namespace login_manager {
///////////////////////////////////////////////////////////////////////////
// NssUtil

// static
NssUtil::Factory* NssUtil::factory_ = NULL;

NssUtil::NssUtil() {}

NssUtil::~NssUtil() {}

///////////////////////////////////////////////////////////////////////////
// NssUtilImpl

class NssUtilImpl : public NssUtil {
 public:
  NssUtilImpl();
  virtual ~NssUtilImpl();

  bool OpenUserDB();

  bool CheckOwnerKey(const std::vector<uint8>& public_key_der);

  FilePath GetOwnerKeyFilePath();

  bool Verify(const uint8* algorithm, int algorithm_len,
              const uint8* signature, int signature_len,
              const uint8* data, int data_len,
              const uint8* public_key, int public_key_len);

 private:
  DISALLOW_COPY_AND_ASSIGN(NssUtilImpl);
};

// Defined here, instead of up above, because we need NssUtilImpl.
// static
NssUtil* NssUtil::Create() {
  if (!factory_)
    return new NssUtilImpl;
  else
    return factory_->CreateNssUtil();
}

// static
void NssUtil::KeyFromBuffer(const GArray* buf, std::vector<uint8>* out) {
  out->resize(buf->len);
  if (buf->len == 0)
    return;
  memcpy(&(out->at(0)), buf->data, buf->len);
}

NssUtilImpl::NssUtilImpl() {}

NssUtilImpl::~NssUtilImpl() {}

bool NssUtilImpl::OpenUserDB() {
  base::EnsureNSSInit();
  // TODO(cmasone): If we ever try to keep the session_manager alive across
  // user sessions, we'll need to deal with the fact that we have no way to
  // close this persistent DB.
  base::OpenPersistentNSSDB();
  return true;
}

bool NssUtilImpl::CheckOwnerKey(const std::vector<uint8>& public_key_der) {
  if (public_key_der.size() == 0) {
    LOG(ERROR) << "Not checking key because size is 0";
    return false;
  }
  scoped_ptr<base::RSAPrivateKey> pair(
      base::RSAPrivateKey::FindFromPublicKeyInfo(public_key_der));
  return pair.get() != NULL;
}

FilePath NssUtilImpl::GetOwnerKeyFilePath() {
  return FilePath(chromeos::kOwnerKeyFile);
}

// This is pretty much just a blind passthrough, so I won't test it
// in the NssUtil unit tests.  I'll test it from a class that uses this API.
bool NssUtilImpl::Verify(const uint8* algorithm, int algorithm_len,
                         const uint8* signature, int signature_len,
                         const uint8* data, int data_len,
                         const uint8* public_key, int public_key_len) {
  base::SignatureVerifier verifier_;

  if (!verifier_.VerifyInit(algorithm, algorithm_len,
                            signature, signature_len,
                            public_key, public_key_len)) {
    LOG(ERROR) << "Could not initialize verifier";
    return false;
  }

  verifier_.VerifyUpdate(data, data_len);
  return (verifier_.VerifyFinal());
}

}  // namespace login_manager
