/**
 * HTTP2MessageRouter.cpp
 *
 * Copyright 2017 Amazon.com, Inc. or its affiliates.
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

#include "AVSUtils/Logging/Logger.h"
#include "AVSUtils/Threading/Executor.h"
#include "ACL/Transport/HTTP2MessageRouter.h"
#include "ACL/Transport/HTTP2Transport.h"

namespace alexaClientSDK {
namespace acl {

using namespace alexaClientSDK::avsUtils;

HTTP2MessageRouter::HTTP2MessageRouter(std::shared_ptr<AuthDelegateInterface> authDelegate,
                                       const std::string& avsEndpoint)
    : MessageRouter(authDelegate,
                            avsEndpoint,
                            std::make_shared<threading::Executor>(),
                            std::make_shared<threading::Executor>()) {
    m_attachmentManager = std::make_shared<AttachmentManager>();
}

HTTP2MessageRouter::~HTTP2MessageRouter() {
}

std::shared_ptr<TransportInterface> HTTP2MessageRouter::createTransport(
        std::shared_ptr<AuthDelegateInterface> authDelegate,
        const std::string& avsEndpoint,
        MessageConsumerInterface* messageConsumerInterface,
        TransportObserverInterface* transportObserverInterface) {
    return std::make_shared<HTTP2Transport>(authDelegate, avsEndpoint, messageConsumerInterface, m_attachmentManager,
            transportObserverInterface);
}

} // namespace acl
} // namespace alexaClientSDK