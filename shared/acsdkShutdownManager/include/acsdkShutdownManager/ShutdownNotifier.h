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

#ifndef ACSDKSHUTDOWNMANAGER_SHUTDOWNNOTIFIER_H_
#define ACSDKSHUTDOWNMANAGER_SHUTDOWNNOTIFIER_H_

#include <memory>

#include <acsdkNotifier/internal/Notifier.h>
#include <acsdkShutdownManagerInterfaces/ShutdownNotifierInterface.h>
#include <AVSCommon/Utils/RequiresShutdown.h>

namespace alexaClientSDK {
namespace acsdkShutdownManager {

/**
 * Relays notification when it's time to shut down.
 */
class ShutdownNotifier : public acsdkNotifier::Notifier<avsCommon::utils::RequiresShutdown> {
public:
    /**
     * Factory method.
     * @return A new instance of @c ShutdownNotifierInterface.
     */
    static std::shared_ptr<acsdkShutdownManagerInterfaces::ShutdownNotifierInterface> createShutdownNotifierInterface();
};

}  // namespace acsdkShutdownManager
}  // namespace alexaClientSDK

#endif  // ACSDKSHUTDOWNMANAGER_SHUTDOWNNOTIFIER_H_
