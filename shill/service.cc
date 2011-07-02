// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>
#include <stdio.h>

#include <map>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/property_accessor.h"
#include "shill/service.h"
#include "shill/service_dbus_adaptor.h"

using std::map;
using std::string;
using std::vector;

namespace shill {
Service::Service(ControlInterface *control_interface,
                 EventDispatcher *dispatcher,
                 const string& name)
    : auto_connect_(false),
      connectable_(false),
      favorite_(false),
      priority_(0),
      save_credentials_(true),
      dispatcher_(dispatcher),
      name_(name),
      available_(false),
      configured_(false),
      configuration_(NULL),
      connection_(NULL),
      adaptor_(control_interface->CreateServiceAdaptor(this)) {

  store_.RegisterBool(flimflam::kAutoConnectProperty, &auto_connect_);
  store_.RegisterString(flimflam::kCheckPortalProperty, &check_portal_);
  store_.RegisterConstBool(flimflam::kConnectableProperty, &connectable_);
  HelpRegisterDerivedString(flimflam::kDeviceProperty,
                            &Service::GetDeviceRpcId,
                            NULL);

  store_.RegisterString(flimflam::kEapIdentityProperty, &eap_.identity);
  store_.RegisterString(flimflam::kEAPEAPProperty, &eap_.eap);
  store_.RegisterString(flimflam::kEapPhase2AuthProperty, &eap_.inner_eap);
  store_.RegisterString(flimflam::kEapAnonymousIdentityProperty,
                        &eap_.anonymous_identity);
  store_.RegisterString(flimflam::kEAPClientCertProperty, &eap_.client_cert);
  store_.RegisterString(flimflam::kEAPCertIDProperty, &eap_.cert_id);
  store_.RegisterString(flimflam::kEapPrivateKeyProperty, &eap_.private_key);
  store_.RegisterString(flimflam::kEapPrivateKeyPasswordProperty,
                        &eap_.private_key_password);
  store_.RegisterString(flimflam::kEAPKeyIDProperty, &eap_.key_id);
  store_.RegisterString(flimflam::kEapCaCertProperty, &eap_.ca_cert);
  store_.RegisterString(flimflam::kEapCaCertIDProperty, &eap_.ca_cert_id);
  store_.RegisterString(flimflam::kEAPPINProperty, &eap_.pin);
  store_.RegisterString(flimflam::kEapPasswordProperty, &eap_.password);
  store_.RegisterString(flimflam::kEapKeyMgmtProperty, &eap_.key_management);
  store_.RegisterBool(flimflam::kEapUseSystemCAsProperty, &eap_.use_system_cas);

  store_.RegisterConstString(flimflam::kErrorProperty, &error_);
  store_.RegisterConstBool(flimflam::kFavoriteProperty, &favorite_);
  HelpRegisterDerivedBool(flimflam::kIsActiveProperty,
                          &Service::IsActive, NULL);
  store_.RegisterConstString(flimflam::kNameProperty, &name_);
  store_.RegisterInt32(flimflam::kPriorityProperty, &priority_);
  HelpRegisterDerivedString(flimflam::kProfileProperty,
                            &Service::GetProfileRpcId,
                            NULL);
  // HelpRegisterDerivedString(flimflam::kProfileProperty, &Getter, &Setter);
  // TODO(cmasone): Create VPN Service with this property
  // store_.RegisterConstStringmap(flimflam::kProviderProperty, &provider_);
  store_.RegisterString(flimflam::kProxyConfigProperty, &proxy_config_);
  store_.RegisterBool(flimflam::kSaveCredentialsProperty, &save_credentials_);
  HelpRegisterDerivedString(flimflam::kStateProperty,
                            &Service::CalculateState,
                            NULL);
  VLOG(2) << "Service initialized.";
}

Service::~Service() {}

string Service::GetRpcIdentifier() {
  return adaptor_->GetRpcIdentifier();
}

void Service::HelpRegisterDerivedBool(const string &name,
                                  bool(Service::*get)(void),
                                  bool(Service::*set)(const bool&)) {
  store_.RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<Service, bool>(this, get, set)));
}

void Service::HelpRegisterDerivedString(const string &name,
                                    string(Service::*get)(void),
                                    bool(Service::*set)(const string&)) {
  store_.RegisterDerivedString(
      name,
      StringAccessor(new CustomAccessor<Service, string>(this, get, set)));
}

}  // namespace shill
