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

#ifndef ALEXA_CLIENT_SDK_INTEGRATION_INCLUDE_INTEGRATION_TESTALERTOBSERVER_H_
#define ALEXA_CLIENT_SDK_INTEGRATION_INCLUDE_INTEGRATION_TESTALERTOBSERVER_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <deque>

#include <acsdkAlertsInterfaces/AlertObserverInterface.h>

namespace alexaClientSDK {
namespace integration {
namespace test {

class TestAlertObserver : public acsdkAlertsInterfaces::AlertObserverInterface {
public:
    void onAlertStateChange(const AlertObserverInterface::AlertInfo& alertInfo) override;

    class changedAlert {
    public:
        State state;
    };

    changedAlert waitForNext(const std::chrono::seconds duration);

private:
    std::mutex m_mutex;
    std::condition_variable m_wakeTrigger;
    std::deque<changedAlert> m_queue;
    State currentState;
};

}  // namespace test
}  // namespace integration
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_INTEGRATION_INCLUDE_INTEGRATION_TESTALERTOBSERVER_H_
