// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_OBJECT_IMPL_H_
#define CHAPS_OBJECT_IMPL_H_

#include "chaps/object.h"

#include <memory>
#include <set>
#include <string>

#include <base/macros.h>

#include "pkcs11/cryptoki.h"

namespace chaps {

class ChapsFactory;
class ObjectPolicy;

class ObjectImpl : public Object {
 public:
  explicit ObjectImpl(ChapsFactory* factory);
  virtual ~ObjectImpl();
  virtual ObjectStage GetStage() const;
  virtual int GetSize() const;
  virtual CK_OBJECT_CLASS GetObjectClass() const;
  virtual bool IsTokenObject() const;
  virtual bool IsModifiable() const;
  virtual bool IsPrivate() const;
  virtual CK_RV FinalizeNewObject();
  virtual CK_RV Copy(const Object* original);
  virtual CK_RV GetAttributes(CK_ATTRIBUTE_PTR attributes,
                              int num_attributes) const;
  virtual CK_RV SetAttributes(const CK_ATTRIBUTE_PTR attributes,
                              int num_attributes);
  virtual bool IsAttributePresent(CK_ATTRIBUTE_TYPE type) const;
  virtual bool GetAttributeBool(CK_ATTRIBUTE_TYPE type,
                                bool default_value) const;
  virtual void SetAttributeBool(CK_ATTRIBUTE_TYPE type, bool value);
  virtual int GetAttributeInt(CK_ATTRIBUTE_TYPE type,
                              int default_value) const;
  virtual void SetAttributeInt(CK_ATTRIBUTE_TYPE type, int value);
  virtual std::string GetAttributeString(CK_ATTRIBUTE_TYPE type) const;
  virtual void SetAttributeString(CK_ATTRIBUTE_TYPE type,
                                  const std::string& value);
  virtual void RemoveAttribute(CK_ATTRIBUTE_TYPE type);
  virtual const AttributeMap* GetAttributeMap() const;
  virtual int handle() const {return handle_;}
  virtual void set_handle(int handle) {handle_ = handle;}
  virtual int store_id() const {return store_id_;}
  virtual void set_store_id(int store_id) {store_id_ = store_id;}

 private:
  ChapsFactory* factory_;
  ObjectStage stage_;
  AttributeMap attributes_;
  // Tracks attributes which have been set by the user.
  std::set<CK_ATTRIBUTE_TYPE> external_attributes_;
  std::unique_ptr<ObjectPolicy> policy_;
  int handle_;
  int store_id_;

  bool SetPolicyByClass();

  DISALLOW_COPY_AND_ASSIGN(ObjectImpl);
};

}  // namespace chaps

#endif  // CHAPS_OBJECT_IMPL_H_
