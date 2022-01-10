/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#ifndef REGISTRATIONMANAGER_REGISTRATIONNOTIFIERINTERFACE_H_
#define REGISTRATIONMANAGER_REGISTRATIONNOTIFIERINTERFACE_H_

#include <acsdkNotifierInterfaces/internal/NotifierInterface.h>

#include "RegistrationManager/RegistrationObserverInterface.h"

namespace alexaClientSDK {
namespace registrationManager {

/**
 * Interface for registering to observe changes to RegistrationManager.
 */
using RegistrationNotifierInterface = acsdkNotifierInterfaces::NotifierInterface<RegistrationObserverInterface>;

}  // namespace registrationManager
}  // namespace alexaClientSDK

#endif  // REGISTRATIONMANAGER_REGISTRATIONNOTIFIERINTERFACE_H_
