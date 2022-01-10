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

#include "AVSCommon/AVS/MessageRequest.h"
#include "AVSCommon/AVS/EditableMessageRequest.h"
#include "AVSCommon/Utils/Logger/Logger.h"

namespace alexaClientSDK {
namespace avsCommon {
namespace avs {

using namespace sdkInterfaces;

/// String to identify log entries originating from this file.
static const std::string TAG("MessageRequest");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param event The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

MessageRequest::MessageRequest(
    const std::string& jsonContent,
    const std::string& uriPathExtension,
    const unsigned int threshold,
    const std::string& streamMetricName) :
        m_jsonContent{jsonContent},
        m_isSerialized{true},
        m_uriPathExtension{uriPathExtension},
        m_streamMetricName{streamMetricName},
        m_streamBytesThreshold{threshold} {
}
MessageRequest::MessageRequest(
    const std::string& jsonContent,
    const unsigned int threshold,
    const std::string& streamMetricName) :
        m_jsonContent{jsonContent},
        m_isSerialized{true},
        m_uriPathExtension{""},
        m_streamMetricName{streamMetricName},
        m_streamBytesThreshold{threshold} {
}
MessageRequest::MessageRequest(
    const std::string& jsonContent,
    bool isSerialized,
    const std::string& uriPathExtension,
    std::vector<std::pair<std::string, std::string>> headers,
    MessageRequestResolveFunction resolver,
    const unsigned int threshold,
    const std::string& streamMetricName) :
        m_jsonContent{jsonContent},
        m_isSerialized{isSerialized},
        m_uriPathExtension{uriPathExtension},
        m_headers(std::move(headers)),
        m_resolver{resolver},
        m_streamMetricName{streamMetricName},
        m_streamBytesThreshold{threshold} {
}

MessageRequest::MessageRequest(const MessageRequest& messageRequest) :
        m_jsonContent{messageRequest.m_jsonContent},
        m_isSerialized{messageRequest.m_isSerialized},
        m_uriPathExtension{messageRequest.m_uriPathExtension},
        m_readers{messageRequest.m_readers},
        m_headers{messageRequest.m_headers},
        m_resolver{messageRequest.m_resolver},
        m_streamMetricName{messageRequest.m_streamMetricName},
        m_streamBytesThreshold{messageRequest.m_streamBytesThreshold} {
}

MessageRequest::~MessageRequest() {
}

void MessageRequest::addAttachmentReader(
    const std::string& name,
    std::shared_ptr<attachment::AttachmentReader> attachmentReader) {
    if (!attachmentReader) {
        ACSDK_ERROR(LX("addAttachmentReaderFailed").d("reason", "nullAttachment"));
        return;
    }

    auto namedReader = std::make_shared<MessageRequest::NamedReader>(name, attachmentReader);
    m_readers.push_back(namedReader);
}

std::string MessageRequest::getJsonContent() const {
    return m_jsonContent;
}

bool MessageRequest::getIsSerialized() const {
    return m_isSerialized;
}

std::string MessageRequest::getUriPathExtension() const {
    return m_uriPathExtension;
}

int MessageRequest::attachmentReadersCount() const {
    return m_readers.size();
}
std::string MessageRequest::getStreamMetricName() const {
    return m_streamMetricName;
}
unsigned int MessageRequest::getStreamBytesThreshold() const {
    return m_streamBytesThreshold;
}

std::shared_ptr<MessageRequest::NamedReader> MessageRequest::getAttachmentReader(size_t index) const {
    if (m_readers.size() <= index) {
        ACSDK_ERROR(LX("getAttachmentReaderFailed").d("reason", "index out of bound").d("index", index));
        return nullptr;
    }

    return m_readers[index];
}

void MessageRequest::responseStatusReceived(avsCommon::sdkInterfaces::MessageRequestObserverInterface::Status status) {
    std::unique_lock<std::mutex> lock{m_observerMutex};
    auto observers = m_observers;
    lock.unlock();

    for (auto observer : observers) {
        observer->onResponseStatusReceived(status);
    }
}

void MessageRequest::sendCompleted(avsCommon::sdkInterfaces::MessageRequestObserverInterface::Status status) {
    std::unique_lock<std::mutex> lock{m_observerMutex};
    auto observers = m_observers;
    lock.unlock();

    for (auto observer : observers) {
        observer->onSendCompleted(status);
    }
}
void MessageRequest::exceptionReceived(const std::string& exceptionMessage) {
    ACSDK_ERROR(LX("onExceptionReceived").d("exception", exceptionMessage));

    std::unique_lock<std::mutex> lock{m_observerMutex};
    auto observers = m_observers;
    lock.unlock();

    for (auto observer : observers) {
        observer->onExceptionReceived(exceptionMessage);
    }
}

void MessageRequest::addObserver(std::shared_ptr<avsCommon::sdkInterfaces::MessageRequestObserverInterface> observer) {
    if (!observer) {
        ACSDK_ERROR(LX("addObserverFailed").d("reason", "nullObserver"));
        return;
    }

    std::lock_guard<std::mutex> lock{m_observerMutex};
    m_observers.insert(observer);
}

void MessageRequest::removeObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::MessageRequestObserverInterface> observer) {
    if (!observer) {
        ACSDK_ERROR(LX("removeObserverFailed").d("reason", "nullObserver"));
        return;
    }

    std::lock_guard<std::mutex> lock{m_observerMutex};
    m_observers.erase(observer);
}

const std::vector<std::pair<std::string, std::string>>& MessageRequest::getHeaders() const {
    return m_headers;
}

bool MessageRequest::isResolved() const {
    return !m_resolver;
}

std::shared_ptr<MessageRequest> MessageRequest::resolveRequest(const std::string& resolveKey) const {
    if (m_resolver) {
        auto editableReq = std::make_shared<EditableMessageRequest>(*this);
        if (m_resolver(editableReq, resolveKey)) {
            // Set MessageRequest to resolved state
            editableReq->setMessageRequestResolveFunction(nullptr);
            return editableReq;
        }
        ACSDK_ERROR(LX("Failed to resolve MessageRequest."));
        return nullptr;
    }
    ACSDK_ERROR(LX("ResolveRequest is called for a resolved MessageRequest."));
    return nullptr;
}

}  // namespace avs
}  // namespace avsCommon
}  // namespace alexaClientSDK
