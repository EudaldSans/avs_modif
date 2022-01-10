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

#include <AVSCommon/AVS/CapabilityConfiguration.h>
#include <AVSCommon/AVS/MessageRequest.h>
#include <AVSCommon/AVS/SpeakerConstants/SpeakerConstants.h>
#include <AVSCommon/Utils/File/FileUtils.h>
#include <AVSCommon/Utils/JSON/JSONUtils.h>
#include <AVSCommon/Utils/Metrics/DataPointCounterBuilder.h>
#include <AVSCommon/Utils/Metrics/DataPointStringBuilder.h>
#include <AVSCommon/Utils/Metrics/MetricEventBuilder.h>
#include <AVSCommon/Utils/Timing/TimeUtils.h>
#include <acsdkManufactory/Annotated.h>
#include <Settings/Setting.h>
#include <Settings/SettingEventMetadata.h>
#include <Settings/SettingEventSender.h>
#include <Settings/SharedAVSSettingProtocol.h>
#include <Settings/Storage/DeviceSettingStorageInterface.h>
#include <Settings/Types/AlarmVolumeRampTypes.h>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <unordered_set>

#include "acsdkAlerts/Alarm.h"
#include "acsdkAlerts/AlertsCapabilityAgent.h"
#include "acsdkAlerts/Reminder.h"
#include "acsdkAlerts/Timer.h"

namespace alexaClientSDK {
namespace acsdkAlerts {

using namespace acsdkAlertsInterfaces;
using namespace avsCommon::avs;
using namespace avsCommon::utils::configuration;
using namespace avsCommon::utils::file;
using namespace avsCommon::utils::json::jsonUtils;
using namespace avsCommon::utils::logger;
using namespace avsCommon::utils::metrics;
using namespace avsCommon::utils::timing;
using namespace avsCommon::sdkInterfaces;
using namespace certifiedSender;
using namespace rapidjson;
using namespace settings;
using namespace settings::types;

/// Alerts capability constants
/// Alerts interface type
static const std::string ALERTS_CAPABILITY_INTERFACE_TYPE = "AlexaInterface";
/// Alerts interface name
static const std::string ALERTS_CAPABILITY_INTERFACE_NAME = "Alerts";
/// Alerts interface version
static const std::string ALERTS_CAPABILITY_INTERFACE_VERSION = "1.5";

/// The value for Type which we need for json parsing.
static const std::string KEY_TYPE = "type";

// ==== Directives ===

/// The value of the SetAlert Directive.
static const std::string DIRECTIVE_NAME_SET_ALERT = "SetAlert";
/// The value of the DeleteAlert Directive.
static const std::string DIRECTIVE_NAME_DELETE_ALERT = "DeleteAlert";
/// The value of the DeleteAlerts Directive.
static const std::string DIRECTIVE_NAME_DELETE_ALERTS = "DeleteAlerts";
/// The value of the SetVolume Directive.
static const std::string DIRECTIVE_NAME_SET_VOLUME = "SetVolume";
/// The value of the AdjustVolume Directive.
static const std::string DIRECTIVE_NAME_ADJUST_VOLUME = "AdjustVolume";
/// The value of the SetAlarmVolumeRamp Directive.
static const std::string DIRECTIVE_NAME_SET_ALARM_VOLUME_RAMP = "SetAlarmVolumeRamp";

// ==== Events ===

/// The value of the SetAlertSucceeded Event name.
static const std::string SET_ALERT_SUCCEEDED_EVENT_NAME = "SetAlertSucceeded";
/// The value of the SetAlertFailed Event name.
static const std::string SET_ALERT_FAILED_EVENT_NAME = "SetAlertFailed";
/// The value of the DeleteAlertSucceeded Event name.
static const std::string DELETE_ALERT_SUCCEEDED_EVENT_NAME = "DeleteAlertSucceeded";
/// The value of the DeleteAlertFailed Event name.
static const std::string DELETE_ALERT_FAILED_EVENT_NAME = "DeleteAlertFailed";
/// The value of the AlertStarted Event name.
static const std::string ALERT_STARTED_EVENT_NAME = "AlertStarted";
/// The value of the AlertStopped Event name.
static const std::string ALERT_STOPPED_EVENT_NAME = "AlertStopped";
/// The value of the AlertEnteredForeground Event name.
static const std::string ALERT_ENTERED_FOREGROUND_EVENT_NAME = "AlertEnteredForeground";
/// The value of the AlertEnteredBackground Event name.
static const std::string ALERT_ENTERED_BACKGROUND_EVENT_NAME = "AlertEnteredBackground";
/// The value of the VolumeChanged Event name.
static const std::string ALERT_VOLUME_CHANGED_EVENT_NAME = "VolumeChanged";
/// The value of the DeleteAlertsSucceeded Event name.
static const std::string ALERT_DELETE_ALERTS_SUCCEEDED_EVENT_NAME = "DeleteAlertsSucceeded";
/// The value of the DeleteAlertsFailed Event name.
static const std::string ALERT_DELETE_ALERTS_FAILED_EVENT_NAME = "DeleteAlertsFailed";
/// The value of the AlarmVolumeRampChanged Event name.
static const std::string ALERT_ALARM_VOLUME_RAMP_CHANGED_EVENT_NAME = "AlarmVolumeRampChanged";
/// The value of the ReportAlarmVolumeRamp Event name.
static const std::string ALERT_REPORT_ALARM_VOLUME_RAMP_EVENT_NAME = "AlarmVolumeRampReport";
// ==== Other constants ===

/// The value of the event payload key for a single token.
static const std::string EVENT_PAYLOAD_TOKEN_KEY = "token";
/// The value of the event payload key for multiple tokens.
static const std::string EVENT_PAYLOAD_TOKENS_KEY = "tokens";
/// The value of the event payload key for scheduled time.
static const std::string EVENT_PAYLOAD_SCHEDULED_TIME_KEY = "scheduledTime";
/// The value of the event payload key for event time.
static const std::string EVENT_PAYLOAD_EVENT_TIME_KEY = "eventTime";
/// The value of Token text in a Directive we may receive.
static const std::string DIRECTIVE_PAYLOAD_TOKEN_KEY = "token";
/// The value of Token list key in a Directive we may receive.
static const std::string DIRECTIVE_PAYLOAD_TOKENS_KEY = "tokens";
/// The value of volume key in a Directive we may receive.
static const std::string DIRECTIVE_PAYLOAD_VOLUME = "volume";
/// The value of alarm volume ramp key in a Directive we may receive.
static const std::string DIRECTIVE_PAYLOAD_ALARM_VOLUME_RAMP = "alarmVolumeRamp";

static const std::string AVS_CONTEXT_HEADER_NAMESPACE_VALUE_KEY = "Alerts";
/// The value of the Alerts Context Names.
static const std::string AVS_CONTEXT_HEADER_NAME_VALUE_KEY = "AlertsState";
/// The value of the Alerts Context allAlerts node.
static const std::string AVS_CONTEXT_ALL_ALERTS_TOKEN_KEY = "allAlerts";
/// The value of the Alerts Context activeAlerts node.
static const std::string AVS_CONTEXT_ACTIVE_ALERTS_TOKEN_KEY = "activeAlerts";
/// The value of the Alerts Context token key.
static const std::string AVS_CONTEXT_ALERT_TOKEN_KEY = "token";
/// The value of the Alerts Context type key.
static const std::string AVS_CONTEXT_ALERT_TYPE_KEY = "type";
/// The value of the Alerts Context scheduled time key.
static const std::string AVS_CONTEXT_ALERT_SCHEDULED_TIME_KEY = "scheduledTime";

/// The value of the volume state info volume key.
static const std::string AVS_PAYLOAD_VOLUME_KEY = "volume";
/// The value of the alarm volume ramp state key for alarm volume ramp events.
static const std::string AVS_PAYLOAD_ALARM_VOLUME_RAMP_KEY = "alarmVolumeRamp";
/// The JSON key in the payload of error events.
static const std::string AVS_PAYLOAD_ERROR_KEY = "error";
/// The JSON key for the error type in the payload of error events.
static const std::string AVS_PAYLOAD_ERROR_TYPE_KEY = "type";
/// The JSON key for the error message in the payload of error events.
static const std::string AVS_PAYLOAD_ERROR_MESSAGE_KEY = "message";

/// The value of the offline stopped alert token key.
static const std::string OFFLINE_STOPPED_ALERT_TOKEN_KEY = "token";
/// The value of the offline stopped alert scheduledTime key.
static const std::string OFFLINE_STOPPED_ALERT_SCHEDULED_TIME_KEY = "scheduledTime";
/// The value of the offline stopped alert eventTime key.
static const std::string OFFLINE_STOPPED_ALERT_EVENT_TIME_KEY = "eventTime";
/// The value of the offline stopped alert id key.
static const std::string OFFLINE_STOPPED_ALERT_ID_KEY = "id";

/// An empty dialogRequestId.
static const std::string EMPTY_DIALOG_REQUEST_ID = "";

/// The namespace for this capability agent.
static const std::string NAMESPACE = "Alerts";
/// The SetAlert directive signature.
static const avsCommon::avs::NamespaceAndName SET_ALERT{NAMESPACE, DIRECTIVE_NAME_SET_ALERT};
/// The DeleteAlert directive signature.
static const avsCommon::avs::NamespaceAndName DELETE_ALERT{NAMESPACE, DIRECTIVE_NAME_DELETE_ALERT};
/// The DeleteAlerts directive signature.
static const avsCommon::avs::NamespaceAndName DELETE_ALERTS{NAMESPACE, DIRECTIVE_NAME_DELETE_ALERTS};
/// The SetVolume directive signature.
static const avsCommon::avs::NamespaceAndName SET_VOLUME{NAMESPACE, DIRECTIVE_NAME_SET_VOLUME};
/// The AdjustVolume directive signature.
static const avsCommon::avs::NamespaceAndName ADJUST_VOLUME{NAMESPACE, DIRECTIVE_NAME_ADJUST_VOLUME};
/// The SetAlarmVolumeRamp directive signature.
static const avsCommon::avs::NamespaceAndName SET_ALARM_VOLUME_RAMP{NAMESPACE, DIRECTIVE_NAME_SET_ALARM_VOLUME_RAMP};

/// String to identify log entries originating from this file.
static const std::string TAG("AlertsCapabilityAgent");

/// Metric Activity Name Prefix for ALERT metric source
static const std::string ALERT_METRIC_SOURCE_PREFIX = "ALERT-";
/// Metric names
static const std::string ALERT_STARTED_METRIC_NAME = "NotificationStartedRinging";
static const std::string ALERT_CANCELED_METRIC_NAME = "NotificationCanceled";
/// Metric metadata keys
static const std::string METRIC_METADATA_TYPE_KEY = "NotificationType";
static const std::string METRIC_METADATA_TOKEN_KEY = "NotificationId";
static const std::string METRIC_METADATA_VERSION_KEY = "NotificationMetadataVersion";
static const std::string METRIC_METADATA_DEVICE_STATE_KEY = "DeviceState";
static const std::string METRIC_METADATA_ACTUAL_TRIGGER_TIME_KEY = "ActualTriggerTime";
static const std::string METRIC_METADATA_SCHEDULED_TRIGGER_TIME_KEY = "ScheduledTriggerTime";
static const std::string METRIC_METADATA_MONOTONIC_TIME_KEY = "MonotonicTime";

static const std::string METRIC_METADATA_IS_ASCENDING_KEY = "IsAscending";
static const std::string METRIC_METADATA_ALERT_VOLUME_KEY = "NotificationVolume";
static const std::string METRIC_METADATA_IS_QUEUED_KEY = "IsNotificationQueued";

static const std::string METRIC_METADATA_CANCELED_REASON_KEY = "CanceledReason";
/// Metric metadata values
static const int METRIC_METADATA_VERSION_VALUE = 2;
static const int MILLISECONDS_IN_A_SECOND = 1000;
static const std::string METRIC_METADATA_IS_QUEUED_VALUE = "false";
static const std::string METRIC_METADATA_DEVICE_STATE_ONLINE = "ONLINE";
static const std::string METRIC_METADATA_DEVICE_STATE_OFFLINE = "OFFLINE";
static const std::string METRIC_METADATA_CANCELED_REASON_VALUE = "TriggerTimeInThePast";

/// Metric constants related to Alerts
static const std::string FAILED_SNOOZE_ALERT = "failedToSnoozeAlert";
static const std::string FAILED_SCHEDULE_ALERT = "failedToScheduleAlert";
static const std::string INVALID_PAYLOAD_FOR_SET_ALARM_VOLUME = "invalidPayloadToSetAlarmRamping";
static const std::string INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME = "invalidPayloadToChangeAlarmVolume";
static const std::string ALERT_RINGING_LESS_THAN_30_PERCENT_MAX_VOLUME =
    "alertTriggeredAtLessThan30PercentMaxAlertVolume";
static const std::string ALERT_RINGING_ZERO_VOLUME = "alertTriggeredAtZeroAlertVolume";
static const int ALERT_VOLUME_METRIC_LIMIT = 30;
/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

/**
 * Creates the alerts capability configuration.
 *
 * @return The alerts capability configuration.
 */
static std::shared_ptr<avsCommon::avs::CapabilityConfiguration> getAlertsCapabilityConfiguration();

/**
 * Utility function to construct a rapidjson array of alert details, representing all the alerts currently managed.
 *
 * @param alertsInfo All the alerts being managed by this Capability Agent.
 * @param allocator The rapidjson allocator, required for the results of this function to be mergable with other
 * rapidjson::Value objects.
 * @return The rapidjson::Value representing the array.
 */
static rapidjson::Value buildAllAlertsContext(
    const std::vector<Alert::ContextInfo>& alertsInfo,
    Document::AllocatorType& allocator) {
    rapidjson::Value alertArray(rapidjson::kArrayType);

    for (const auto& info : alertsInfo) {
        rapidjson::Value alertJson;
        alertJson.SetObject();
        alertJson.AddMember(StringRef(AVS_CONTEXT_ALERT_TOKEN_KEY), info.token, allocator);
        alertJson.AddMember(StringRef(AVS_CONTEXT_ALERT_TYPE_KEY), info.type, allocator);
        alertJson.AddMember(StringRef(AVS_CONTEXT_ALERT_SCHEDULED_TIME_KEY), info.scheduledTime_ISO_8601, allocator);

        alertArray.PushBack(alertJson, allocator);
    }

    return alertArray;
}

/**
 * Generate a UTC ISO8601-formatted timestamp
 * @return UTC ISO8601-formatted timestamp as std::string
 */
static std::string currentISO8601TimeUTC() {
    char buf[sizeof("2011-10-08T07:07:09Z")];
    auto now = std::chrono::system_clock::now();
    auto itt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&itt));
    ss << buf;
    return ss.str();
}

/**
 * Utility function to construct a rapidjson array of alert details, representing all the currently active alerts.
 *
 * @param alertsInfo The currently active alert, which may be nullptr if no alert is active.
 * @param allocator The rapidjson allocator, required for the results of this function to be mergable with other
 * rapidjson::Value objects.
 * @return The rapidjson::Value representing the array.
 */
static rapidjson::Value buildActiveAlertsContext(
    const std::vector<Alert::ContextInfo>& alertsInfo,
    Document::AllocatorType& allocator) {
    rapidjson::Value alertArray(rapidjson::kArrayType);

    if (!alertsInfo.empty()) {
        auto& info = alertsInfo[0];
        rapidjson::Value alertJson;
        alertJson.SetObject();
        alertJson.AddMember(StringRef(AVS_CONTEXT_ALERT_TOKEN_KEY), info.token, allocator);
        alertJson.AddMember(StringRef(AVS_CONTEXT_ALERT_TYPE_KEY), info.type, allocator);
        alertJson.AddMember(StringRef(AVS_CONTEXT_ALERT_SCHEDULED_TIME_KEY), info.scheduledTime_ISO_8601, allocator);

        alertArray.PushBack(alertJson, allocator);
    }

    return alertArray;
}

/**
 * Submits a metric for a given count and name
 * @param metricRecorder The @c MetricRecorderInterface which records Metric events
 * @param eventName The name of the metric event
 * @param count The count for metric event
 */
static void submitMetric(
    const std::shared_ptr<MetricRecorderInterface>& metricRecorder,
    const std::string& eventName,
    int count) {
    if (!metricRecorder) {
        return;
    }

    auto metricEvent = MetricEventBuilder{}
                           .setActivityName(ALERT_METRIC_SOURCE_PREFIX + eventName)
                           .addDataPoint(DataPointCounterBuilder{}.setName(eventName).increment(count).build())
                           .build();

    if (metricEvent == nullptr) {
        ACSDK_ERROR(LX("Error creating metric."));
        return;
    }
    recordMetric(metricRecorder, metricEvent);
}

std::shared_ptr<AlertsCapabilityAgentInterface> AlertsCapabilityAgent::createAlertsCapabilityAgent(
    const std::shared_ptr<acsdkAlerts::renderer::Renderer>& alertRenderer,
    const std::shared_ptr<acsdkShutdownManagerInterfaces::ShutdownNotifierInterface>& shutdownNotifier,
    const std::shared_ptr<avsCommon::sdkInterfaces::AVSConnectionManagerInterface>& connectionManager,
    const std::shared_ptr<avsCommon::sdkInterfaces::ContextManagerInterface>& contextManager,
    const std::shared_ptr<avsCommon::sdkInterfaces::ExceptionEncounteredSenderInterface>& exceptionEncounteredSender,
    const acsdkManufactory::Annotated<
        avsCommon::sdkInterfaces::AudioFocusAnnotation,
        avsCommon::sdkInterfaces::FocusManagerInterface>& audioFocusManager,
    const std::shared_ptr<avsCommon::sdkInterfaces::MessageSenderInterface>& messageSender,
    const std::shared_ptr<avsCommon::sdkInterfaces::SpeakerManagerInterface>& speakerManager,
    const std::shared_ptr<avsCommon::sdkInterfaces::audio::AudioFactoryInterface>& audioFactory,
    const acsdkManufactory::Annotated<
        avsCommon::sdkInterfaces::endpoints::DefaultEndpointAnnotation,
        avsCommon::sdkInterfaces::endpoints::EndpointCapabilitiesRegistrarInterface>& endpointCapabilitiesRegistrar,
    const std::shared_ptr<avsCommon::utils::metrics::MetricRecorderInterface>& metricRecorder,
    const std::shared_ptr<acsdkSystemClockMonitorInterfaces::SystemClockNotifierInterface>& systemClockMonitor,
    const std::shared_ptr<certifiedSender::CertifiedSender>& certifiedSender,
    const std::shared_ptr<registrationManager::CustomerDataManagerInterface>& dataManager,
    const std::shared_ptr<settings::DeviceSettingsManager>& settingsManager,
    const std::shared_ptr<storage::AlertStorageInterface>& alertStorage,
    bool startAlertSchedulingOnInitialization) {
    if (!alertRenderer || !shutdownNotifier || !connectionManager || !contextManager || !exceptionEncounteredSender ||
        !audioFocusManager || !messageSender || !speakerManager || !audioFactory || !endpointCapabilitiesRegistrar ||
        !systemClockMonitor || !certifiedSender || !dataManager || !settingsManager || !alertStorage) {
        ACSDK_ERROR(LX("createAlertsCapabilityAgentFailed")
                        .d("isAlertRendererNull", !alertRenderer)
                        .d("isShutdownNotifierNull", !shutdownNotifier)
                        .d("isConnectionManagerNull", !connectionManager)
                        .d("isContextManagerNull", !contextManager)
                        .d("isExceptionEncounteredSenderNull", !exceptionEncounteredSender)
                        .d("isAudioFocusManagerNull", !audioFocusManager)
                        .d("isMessageSenderNull", !messageSender)
                        .d("isSpeakerManagerNull", !speakerManager)
                        .d("isAudioFactoryNull", !audioFactory)
                        .d("isEndpointCapabilitiesRegistrarNull", !endpointCapabilitiesRegistrar)
                        .d("isSystemClockMonitorNull", !systemClockMonitor)
                        .d("isCertifiedSenderNull", !certifiedSender)
                        .d("isDataManagerNull", !dataManager));
        return nullptr;
    }

    std::shared_ptr<FocusManagerInterface> focusManager = audioFocusManager;
    auto alarmVolumeRampSetting = settingsManager->getSetting<settings::ALARM_VOLUME_RAMP>();
    auto alertsAudioFactory = audioFactory->alerts();

    auto alertsCapabilityAgent = create(
        messageSender,
        connectionManager,
        certifiedSender,
        focusManager,
        speakerManager,
        contextManager,
        exceptionEncounteredSender,
        alertStorage,
        alertsAudioFactory,
        alertRenderer,
        dataManager,
        alarmVolumeRampSetting,
        settingsManager,
        metricRecorder,
        startAlertSchedulingOnInitialization,
        systemClockMonitor);

    shutdownNotifier->addObserver(alertsCapabilityAgent);
    systemClockMonitor->addObserver(alertsCapabilityAgent);
    endpointCapabilitiesRegistrar->withCapability(alertsCapabilityAgent, alertsCapabilityAgent);

    return alertsCapabilityAgent;
}

std::shared_ptr<AlertsCapabilityAgent> AlertsCapabilityAgent::create(
    std::shared_ptr<MessageSenderInterface> messageSender,
    std::shared_ptr<AVSConnectionManagerInterface> connectionManager,
    std::shared_ptr<certifiedSender::CertifiedSender> certifiedMessageSender,
    std::shared_ptr<FocusManagerInterface> focusManager,
    std::shared_ptr<SpeakerManagerInterface> speakerManager,
    std::shared_ptr<ContextManagerInterface> contextManager,
    std::shared_ptr<ExceptionEncounteredSenderInterface> exceptionEncounteredSender,
    std::shared_ptr<storage::AlertStorageInterface> alertStorage,
    std::shared_ptr<audio::AlertsAudioFactoryInterface> alertsAudioFactory,
    std::shared_ptr<renderer::RendererInterface> alertRenderer,
    std::shared_ptr<registrationManager::CustomerDataManagerInterface> dataManager,
    std::shared_ptr<settings::AlarmVolumeRampSetting> alarmVolumeRampSetting,
    std::shared_ptr<settings::DeviceSettingsManager> settingsManager,
    std::shared_ptr<avsCommon::utils::metrics::MetricRecorderInterface> metricRecorder,
    bool startAlertSchedulingOnInitialization,
    std::shared_ptr<acsdkSystemClockMonitorInterfaces::SystemClockNotifierInterface> systemClockMonitor) {
    if (!alarmVolumeRampSetting) {
        ACSDK_ERROR(LX("createFailed").d("reason", "nullAlarmVolumeRampSetting"));
        return nullptr;
    }

    if (!settingsManager) {
        ACSDK_ERROR(LX("createFailed").d("reason", "nullSettingsManager"));
        return nullptr;
    }

    auto alertsCA = std::shared_ptr<AlertsCapabilityAgent>(new AlertsCapabilityAgent(
        messageSender,
        certifiedMessageSender,
        focusManager,
        speakerManager,
        contextManager,
        exceptionEncounteredSender,
        alertStorage,
        alertsAudioFactory,
        alertRenderer,
        dataManager,
        alarmVolumeRampSetting,
        settingsManager,
        metricRecorder,
        systemClockMonitor));

    if (!alertsCA->initialize(startAlertSchedulingOnInitialization)) {
        ACSDK_ERROR(LX("createFailed").d("reason", "Initialization error."));
        return nullptr;
    }

    focusManager->addObserver(alertsCA);
    connectionManager->addConnectionStatusObserver(alertsCA);
    speakerManager->addSpeakerManagerObserver(alertsCA);

    return alertsCA;
}

avsCommon::avs::DirectiveHandlerConfiguration AlertsCapabilityAgent::getConfiguration() const {
    auto audioNonBlockingPolicy = BlockingPolicy(BlockingPolicy::MEDIUM_AUDIO, false);
    auto neitherNonBlockingPolicy = BlockingPolicy(BlockingPolicy::MEDIUMS_NONE, false);

    avsCommon::avs::DirectiveHandlerConfiguration configuration;
    configuration[SET_ALERT] = neitherNonBlockingPolicy;
    configuration[DELETE_ALERT] = neitherNonBlockingPolicy;
    configuration[DELETE_ALERTS] = neitherNonBlockingPolicy;
    configuration[SET_VOLUME] = audioNonBlockingPolicy;
    configuration[ADJUST_VOLUME] = audioNonBlockingPolicy;
    configuration[SET_ALARM_VOLUME_RAMP] = audioNonBlockingPolicy;
    return configuration;
}

void AlertsCapabilityAgent::handleDirectiveImmediately(std::shared_ptr<avsCommon::avs::AVSDirective> directive) {
    if (!directive) {
        ACSDK_ERROR(LX("handleDirectiveImmediatelyFailed").d("reason", "directive is nullptr."));
    }
    auto info = createDirectiveInfo(directive, nullptr);
    m_executor.submit([this, info]() { executeHandleDirectiveImmediately(info); });
}

void AlertsCapabilityAgent::preHandleDirective(std::shared_ptr<DirectiveInfo> info) {
    // intentional no-op.
}

void AlertsCapabilityAgent::handleDirective(std::shared_ptr<DirectiveInfo> info) {
    if (!info) {
        ACSDK_ERROR(LX("handleDirectiveFailed").d("reason", "info is nullptr."));
    }
    m_executor.submit([this, info]() { executeHandleDirectiveImmediately(info); });
}

void AlertsCapabilityAgent::cancelDirective(std::shared_ptr<DirectiveInfo> info) {
    // intentional no-op.
}

void AlertsCapabilityAgent::onDeregistered() {
    // intentional no-op.
}

void AlertsCapabilityAgent::onConnectionStatusChanged(const Status status, const ChangedReason reason) {
    m_executor.submit([this, status, reason]() { executeOnConnectionStatusChanged(status, reason); });
}

void AlertsCapabilityAgent::onFocusChanged(
    avsCommon::avs::FocusState focusState,
    avsCommon::avs::MixingBehavior behavior) {
    ACSDK_DEBUG1(LX("onFocusChanged").d("focusState", focusState).d("mixingBehavior", behavior));

    m_alertScheduler.updateFocus(focusState, behavior);
}

void AlertsCapabilityAgent::onFocusChanged(const std::string& channelName, avsCommon::avs::FocusState newFocus) {
    bool stateIsActive = newFocus != FocusState::NONE;

    if (FocusManagerInterface::CONTENT_CHANNEL_NAME == channelName) {
        m_contentChannelIsActive = stateIsActive;
    } else if (FocusManagerInterface::COMMUNICATIONS_CHANNEL_NAME == channelName) {
        m_commsChannelIsActive = stateIsActive;
    } else {
        return;
    }

    if (m_alertIsSounding) {
        if (!m_commsChannelIsActive && !m_contentChannelIsActive) {
            // All lower channels of interest are stopped playing content. Return alert volume to base value
            // if needed.
            SpeakerInterface::SpeakerSettings speakerSettings;
            if (!getAlertVolumeSettings(&speakerSettings)) {
                ACSDK_ERROR(LX("executeOnFocusChangedFailed").d("reason", "Failed to get speaker settings."));
                return;
            }

            if (speakerSettings.volume > m_lastReportedSpeakerSettings.volume) {
                // Alert is sounding with volume higher than Base Volume. Assume that it was adjusted because of
                // content being played and reset it to the base one. Keep lower values, though.
                // Do not send a volumeChanged event
                m_speakerManager->setVolume(
                    ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME,
                    m_lastReportedSpeakerSettings.volume,
                    SpeakerManagerInterface::NotificationProperties(
                        SpeakerManagerObserverInterface::Source::DIRECTIVE, false, false));
            }
        }
    }
}

void AlertsCapabilityAgent::onAlertStateChange(const AlertObserverInterface::AlertInfo& alertInfo) {
    ACSDK_DEBUG9(LX("onAlertStateChange")
                     .d("alertToken", alertInfo.token)
                     .d("alertType", alertInfo.type)
                     .d("state", alertInfo.state)
                     .d("reason", alertInfo.reason));
    m_executor.submit([this, alertInfo]() { executeOnAlertStateChange(alertInfo); });
}

void AlertsCapabilityAgent::addObserver(std::shared_ptr<AlertObserverInterface> observer) {
    if (!observer) {
        ACSDK_ERROR(LX("addObserverFailed").d("reason", "nullObserver"));
        return;
    }

    m_executor.submit([this, observer]() { executeAddObserver(observer); });
}

void AlertsCapabilityAgent::removeObserver(std::shared_ptr<AlertObserverInterface> observer) {
    if (!observer) {
        ACSDK_ERROR(LX("removeObserverFailed").d("reason", "nullObserver"));
        return;
    }

    m_executor.submit([this, observer]() { executeRemoveObserver(observer); });
}

void AlertsCapabilityAgent::removeAllAlerts() {
    m_executor.submit([this]() { executeRemoveAllAlerts(); });
}

void AlertsCapabilityAgent::onLocalStop() {
    ACSDK_DEBUG9(LX("onLocalStop"));
    m_executor.submitToFront([this]() { executeOnLocalStop(); });
}

AlertsCapabilityAgent::AlertsCapabilityAgent(
    std::shared_ptr<MessageSenderInterface> messageSender,
    std::shared_ptr<certifiedSender::CertifiedSender> certifiedMessageSender,
    std::shared_ptr<FocusManagerInterface> focusManager,
    std::shared_ptr<SpeakerManagerInterface> speakerManager,
    std::shared_ptr<ContextManagerInterface> contextManager,
    std::shared_ptr<ExceptionEncounteredSenderInterface> exceptionEncounteredSender,
    std::shared_ptr<storage::AlertStorageInterface> alertStorage,
    std::shared_ptr<audio::AlertsAudioFactoryInterface> alertsAudioFactory,
    std::shared_ptr<renderer::RendererInterface> alertRenderer,
    std::shared_ptr<registrationManager::CustomerDataManagerInterface> dataManager,
    std::shared_ptr<settings::AlarmVolumeRampSetting> alarmVolumeRampSetting,
    std::shared_ptr<settings::DeviceSettingsManager> settingsManager,
    std::shared_ptr<avsCommon::utils::metrics::MetricRecorderInterface> metricRecorder,
    std::shared_ptr<acsdkSystemClockMonitorInterfaces::SystemClockNotifierInterface> systemClockMonitor) :
        CapabilityAgent("Alerts", exceptionEncounteredSender),
        RequiresShutdown("AlertsCapabilityAgent"),
        CustomerDataHandler(dataManager),
        m_metricRecorder{metricRecorder},
        m_messageSender{messageSender},
        m_certifiedSender{certifiedMessageSender},
        m_focusManager{focusManager},
        m_speakerManager{speakerManager},
        m_contextManager{contextManager},
        m_isConnected{false},
        m_alertScheduler{alertStorage, alertRenderer, ALERT_PAST_DUE_CUTOFF_MINUTES, metricRecorder},
        m_alertsAudioFactory{alertsAudioFactory},
        m_contentChannelIsActive{false},
        m_commsChannelIsActive{false},
        m_alertIsSounding{false},
        m_startSystemClock{std::clock()},
        m_alarmVolumeRampSetting{alarmVolumeRampSetting},
        m_settingsManager{settingsManager},
        m_systemClockMonitor{systemClockMonitor} {
    m_capabilityConfigurations.insert(getAlertsCapabilityConfiguration());
}

std::shared_ptr<CapabilityConfiguration> getAlertsCapabilityConfiguration() {
    std::unordered_map<std::string, std::string> configMap;
    configMap.insert({CAPABILITY_INTERFACE_TYPE_KEY, ALERTS_CAPABILITY_INTERFACE_TYPE});
    configMap.insert({CAPABILITY_INTERFACE_NAME_KEY, ALERTS_CAPABILITY_INTERFACE_NAME});
    configMap.insert({CAPABILITY_INTERFACE_VERSION_KEY, ALERTS_CAPABILITY_INTERFACE_VERSION});

    return std::make_shared<CapabilityConfiguration>(configMap);
}

static void addGenericMetadata(
    std::unordered_map<std::string, std::string>& metadata,
    const std::string& alertToken,
    const std::string& alertType,
    bool isConnected,
    long monotonicTime,
    const std::string& scheduledTriggerTime,
    const std::string& actualTriggerTime) {
    metadata.insert({METRIC_METADATA_TYPE_KEY, alertType});
    metadata.insert({METRIC_METADATA_TOKEN_KEY, alertToken});
    metadata.insert({METRIC_METADATA_VERSION_KEY, std::to_string(METRIC_METADATA_VERSION_VALUE)});
    metadata.insert(
        {METRIC_METADATA_DEVICE_STATE_KEY,
         std::string(((isConnected ? METRIC_METADATA_DEVICE_STATE_ONLINE : METRIC_METADATA_DEVICE_STATE_OFFLINE)))});
    metadata.insert({METRIC_METADATA_ACTUAL_TRIGGER_TIME_KEY, actualTriggerTime});
    metadata.insert({METRIC_METADATA_SCHEDULED_TRIGGER_TIME_KEY, scheduledTriggerTime});
    metadata.insert({METRIC_METADATA_MONOTONIC_TIME_KEY, std::to_string(monotonicTime)});
}

static void addAlertStartedRingingMetadata(
    std::unordered_map<std::string, std::string>& metadata,
    const std::string& ascending,
    int volume) {
    metadata.insert({METRIC_METADATA_IS_ASCENDING_KEY, ascending});
    metadata.insert({METRIC_METADATA_ALERT_VOLUME_KEY, std::to_string(volume)});
    metadata.insert({METRIC_METADATA_IS_QUEUED_KEY, METRIC_METADATA_IS_QUEUED_VALUE});
}

static void addAlertCanceledMetadata(std::unordered_map<std::string, std::string>& metadata) {
    metadata.insert({METRIC_METADATA_CANCELED_REASON_KEY, METRIC_METADATA_CANCELED_REASON_VALUE});
}

static void submitMetricWithMetadata(
    const std::shared_ptr<MetricRecorderInterface>& metricRecorder,
    const std::string& eventName,
    std::unordered_map<std::string, std::string> metadata) {
    if (!metricRecorder) {
        return;
    }

    std::vector<DataPoint> dataPoints;

    for (auto const& pair : metadata) {
        dataPoints.push_back(DataPointStringBuilder{}.setName(pair.first).setValue(pair.second).build());
    }

    auto metricEvent = MetricEventBuilder{}
                           .setActivityName("ALERT-" + eventName)
                           .addDataPoint(DataPointCounterBuilder{}.setName(eventName).increment(1).build())
                           .addDataPoints(dataPoints)
                           .build();

    if (metricEvent == nullptr) {
        ACSDK_ERROR(LX("Error creating metric."));
        return;
    }
    metricRecorder->recordMetric(metricEvent);
}

void AlertsCapabilityAgent::doShutdown() {
    if (m_systemClockMonitor) {
        m_systemClockMonitor->removeObserver(shared_from_this());
        m_systemClockMonitor.reset();
    }
    m_executor.shutdown();
    releaseChannel();
    m_messageSender.reset();
    m_certifiedSender.reset();
    m_focusManager.reset();
    m_contextManager.reset();
    m_observers.clear();
    m_alertScheduler.shutdown();
}

bool AlertsCapabilityAgent::initialize(bool startAlertSchedulingOnInitialization) {
    if (!initializeAlerts(startAlertSchedulingOnInitialization)) {
        ACSDK_ERROR(LX("initializeFailed").m("Could not initialize alerts."));
        return false;
    }

    // Initialize stored value for AVS_ALERTS_VOLUME speaker settings
    if (!getAlertVolumeSettings(&m_lastReportedSpeakerSettings)) {
        return false;
    }

    updateContextManager();

    return true;
}

bool AlertsCapabilityAgent::initializeAlerts(bool startAlertSchedulingOnInitialization) {
    return m_alertScheduler.initialize(shared_from_this(), m_settingsManager, startAlertSchedulingOnInitialization);
}

settings::SettingEventMetadata AlertsCapabilityAgent::getAlarmVolumeRampMetadata() {
    return settings::SettingEventMetadata{
        NAMESPACE,
        ALERT_ALARM_VOLUME_RAMP_CHANGED_EVENT_NAME,
        ALERT_REPORT_ALARM_VOLUME_RAMP_EVENT_NAME,
        AVS_PAYLOAD_ALARM_VOLUME_RAMP_KEY,
    };
}

int AlertsCapabilityAgent::getAlertVolume() {
    SpeakerInterface::SpeakerSettings speakerSettings;
    if (!getAlertVolumeSettings(&speakerSettings)) {
        ACSDK_ERROR(LX("getAlertVolume").d("reason", "Failed to get speaker settings."));
        return -1;
    } else {
        return speakerSettings.volume;
    }
}

bool AlertsCapabilityAgent::handleSetAlert(
    const std::shared_ptr<avsCommon::avs::AVSDirective>& directive,
    const rapidjson::Document& payload,
    std::string* alertToken) {
    ACSDK_DEBUG9(LX("handleSetAlert"));
    std::string alertType;
    if (!retrieveValue(payload, KEY_TYPE, &alertType)) {
        std::string errorMessage = "Alert type not specified for SetAlert";
        ACSDK_ERROR(LX("handleSetAlertFailed").m(errorMessage));
        sendProcessingDirectiveException(directive, errorMessage);
        return false;
    }

    std::shared_ptr<Alert> parsedAlert;

    if (Alarm::getTypeNameStatic() == alertType) {
        parsedAlert = std::make_shared<Alarm>(
            m_alertsAudioFactory->alarmDefault(), m_alertsAudioFactory->alarmShort(), m_settingsManager);
    } else if (Timer::getTypeNameStatic() == alertType) {
        parsedAlert = std::make_shared<Timer>(
            m_alertsAudioFactory->timerDefault(), m_alertsAudioFactory->timerShort(), m_settingsManager);
    } else if (Reminder::getTypeNameStatic() == alertType) {
        parsedAlert = std::make_shared<Reminder>(
            m_alertsAudioFactory->reminderDefault(), m_alertsAudioFactory->reminderShort(), m_settingsManager);
    }

    if (!parsedAlert) {
        ACSDK_ERROR(LX("handleSetAlertFailed").d("reason", "unknown alert type").d("type:", alertType));
        return false;
    }

    std::string errorMessage;

    auto parseStatus = parsedAlert->parseFromJson(payload, &errorMessage);
    if (Alert::ParseFromJsonStatus::MISSING_REQUIRED_PROPERTY == parseStatus) {
        sendProcessingDirectiveException(directive, "Missing required property.");
        return false;
    } else if (Alert::ParseFromJsonStatus::INVALID_VALUE == parseStatus) {
        sendProcessingDirectiveException(directive, "Invalid value.");
        return false;
    }

    *alertToken = parsedAlert->getToken();

    if (m_alertScheduler.isAlertActive(parsedAlert)) {
        if (!m_alertScheduler.snoozeAlert(parsedAlert->getToken(), parsedAlert->getScheduledTime_ISO_8601())) {
            ACSDK_ERROR(LX("handleSetAlertFailed").d("reason", "failed to snooze alert"));
            submitMetric(m_metricRecorder, FAILED_SNOOZE_ALERT, 1);
            return false;
        }

        // Pass the scheduled time to the observers as the reason for the alert created
        executeNotifyObservers(AlertObserverInterface::AlertInfo(
            parsedAlert->getToken(),
            parsedAlert->getType(),
            State::SCHEDULED_FOR_LATER,
            parsedAlert->getScheduledTime_Utc_TimePoint(),
            parsedAlert->getOriginalTime(),
            parsedAlert->getLabel()));
        submitMetric(m_metricRecorder, FAILED_SNOOZE_ALERT, 0);
        submitMetric(m_metricRecorder, "alarmSnoozeCount", 1);
        return true;
    }

    if (!m_alertScheduler.scheduleAlert(parsedAlert)) {
        submitMetric(m_metricRecorder, FAILED_SCHEDULE_ALERT, 1);
        return false;
    }
    submitMetric(m_metricRecorder, FAILED_SCHEDULE_ALERT, 0);

    executeNotifyObservers(AlertObserverInterface::AlertInfo(
        parsedAlert->getToken(),
        parsedAlert->getType(),
        State::SCHEDULED_FOR_LATER,
        parsedAlert->getScheduledTime_Utc_TimePoint(),
        parsedAlert->getOriginalTime(),
        parsedAlert->getLabel()));

    updateContextManager();

    return true;
}

bool AlertsCapabilityAgent::handleDeleteAlert(
    const std::shared_ptr<avsCommon::avs::AVSDirective>& directive,
    const rapidjson::Document& payload,
    std::string* alertToken) {
    ACSDK_DEBUG5(LX(__func__));
    if (!retrieveValue(payload, DIRECTIVE_PAYLOAD_TOKEN_KEY, alertToken)) {
        ACSDK_ERROR(LX("handleDeleteAlertFailed").m("Could not find token in the payload."));
        return false;
    }

    if (!m_alertScheduler.deleteAlert(*alertToken)) {
        submitMetric(m_metricRecorder, "failedToDeleteAlert", 1);
        return false;
    }

    submitMetric(m_metricRecorder, "failedToDeleteAlert", 0);
    updateContextManager();

    return true;
}

bool AlertsCapabilityAgent::handleDeleteAlerts(
    const std::shared_ptr<avsCommon::avs::AVSDirective>& directive,
    const rapidjson::Document& payload) {
    ACSDK_DEBUG5(LX(__func__));

    std::list<std::string> alertTokens;

    auto tokensPayload = payload.FindMember(DIRECTIVE_PAYLOAD_TOKENS_KEY.c_str());
    if (tokensPayload == payload.MemberEnd()) {
        ACSDK_ERROR(LX("handleDeleteAlertsFailed").d("reason", "Cannot find tokens in payload"));
        return false;
    }

    if (!tokensPayload->value.IsArray()) {
        ACSDK_ERROR(LX("handleDeleteAlertsFailed")
                        .d("reason", "value is expected to be an array")
                        .d("key", DIRECTIVE_PAYLOAD_TOKENS_KEY.c_str()));
        return false;
    }

    auto tokenArray = tokensPayload->value.GetArray();
    for (rapidjson::SizeType i = 0; i < tokenArray.Size(); i++) {
        std::string token;
        if (!convertToValue(tokenArray[i], &token)) {
            ACSDK_WARN(LX("handleDeleteAlertsFailed").d("reason", "invalid token in payload"));
            continue;
        }
        alertTokens.push_back(token);
    }

    if (!m_alertScheduler.deleteAlerts(alertTokens)) {
        sendBulkEvent(ALERT_DELETE_ALERTS_FAILED_EVENT_NAME, alertTokens, true);
        return false;
    }

    sendBulkEvent(ALERT_DELETE_ALERTS_SUCCEEDED_EVENT_NAME, alertTokens, true);
    updateContextManager();

    return true;
}

bool AlertsCapabilityAgent::handleSetVolume(
    const std::shared_ptr<avsCommon::avs::AVSDirective>& directive,
    const rapidjson::Document& payload) {
    ACSDK_DEBUG5(LX(__func__));
    int64_t volumeValue = 0;
    if (!retrieveValue(payload, DIRECTIVE_PAYLOAD_VOLUME, &volumeValue)) {
        ACSDK_ERROR(LX("handleSetVolumeFailed").m("Could not find volume in the payload."));
        submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME, 1);
        return false;
    }

    submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME, 0);
    setNextAlertVolume(volumeValue);

    return true;
}

bool AlertsCapabilityAgent::handleAdjustVolume(
    const std::shared_ptr<avsCommon::avs::AVSDirective>& directive,
    const rapidjson::Document& payload) {
    ACSDK_DEBUG5(LX(__func__));
    int64_t adjustValue = 0;
    if (!retrieveValue(payload, DIRECTIVE_PAYLOAD_VOLUME, &adjustValue)) {
        ACSDK_ERROR(LX("handleAdjustVolumeFailed").m("Could not find volume in the payload."));
        submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME, 1);
        return false;
    }
    submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME, 0);

    SpeakerInterface::SpeakerSettings speakerSettings;
    if (!m_speakerManager->getSpeakerSettings(ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME, &speakerSettings)
             .get()) {
        ACSDK_ERROR(LX("handleAdjustVolumeFailed").m("Could not retrieve speaker volume."));
        return false;
    }
    int64_t volume = adjustValue + speakerSettings.volume;

    setNextAlertVolume(volume);

    return true;
}

bool AlertsCapabilityAgent::handleSetAlarmVolumeRamp(
    const std::shared_ptr<avsCommon::avs::AVSDirective>& directive,
    const rapidjson::Document& payload) {
    std::string jsonValue;
    if (!retrieveValue(payload, DIRECTIVE_PAYLOAD_ALARM_VOLUME_RAMP, &jsonValue)) {
        std::string errorMessage =
            DIRECTIVE_PAYLOAD_ALARM_VOLUME_RAMP + " not specified for " + DIRECTIVE_NAME_SET_ALARM_VOLUME_RAMP;
        ACSDK_ERROR(LX("handleSetAlarmVolumeRampFailed").m(errorMessage));
        sendProcessingDirectiveException(directive, errorMessage);
        submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_SET_ALARM_VOLUME, 1);
        return false;
    }

    submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_SET_ALARM_VOLUME, 0);
    auto value = getAlarmVolumeRampDefault();
    std::stringstream ss{jsonValue};
    ss >> value;
    if (ss.fail()) {
        ACSDK_ERROR(LX(__func__).d("error", "invalid").d("value", jsonValue));
        submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME, 1);
        return false;
    }

    submitMetric(m_metricRecorder, INVALID_PAYLOAD_FOR_CHANGE_ALARM_VOLUME, 0);
    return m_alarmVolumeRampSetting->setAvsChange(value);
}

void AlertsCapabilityAgent::sendEvent(
    const std::string& eventName,
    const std::string& alertToken,
    bool isCertified,
    const std::string& scheduledTime,
    const std::string& eventTime) {
    submitMetric(m_metricRecorder, eventName, 1);
    rapidjson::Document payload(kObjectType);
    rapidjson::Document::AllocatorType& alloc = payload.GetAllocator();

    payload.AddMember(StringRef(EVENT_PAYLOAD_TOKEN_KEY), alertToken, alloc);

    if (ALERT_STARTED_EVENT_NAME == eventName || ALERT_STOPPED_EVENT_NAME == eventName) {
        payload.AddMember(StringRef(EVENT_PAYLOAD_SCHEDULED_TIME_KEY), scheduledTime, alloc);
        payload.AddMember(StringRef(EVENT_PAYLOAD_EVENT_TIME_KEY), eventTime, alloc);
        if (!m_isConnected && (ALERT_STOPPED_EVENT_NAME == eventName)) {
            m_alertScheduler.saveOfflineStoppedAlert(alertToken, scheduledTime, eventTime);
            return;
        }
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!payload.Accept(writer)) {
        return;
    }

    auto jsonEventString = buildJsonEventString(eventName, EMPTY_DIALOG_REQUEST_ID, buffer.GetString()).second;

    if (isCertified) {
        m_certifiedSender->sendJSONMessage(jsonEventString);
    } else {
        if (!m_isConnected) {
            ACSDK_WARN(
                LX("sendEvent").m("Not connected to AVS.  Not sending Event.").d("event details", jsonEventString));
        } else {
            auto request = std::make_shared<MessageRequest>(jsonEventString);
            m_messageSender->sendMessage(request);
        }
    }
}

void AlertsCapabilityAgent::sendBulkEvent(
    const std::string& eventName,
    const std::list<std::string>& tokenList,
    bool isCertified) {
    submitMetric(m_metricRecorder, eventName, 1);
    rapidjson::Document payload(kObjectType);
    rapidjson::Document::AllocatorType& alloc = payload.GetAllocator();

    rapidjson::Value jsonTokenList(kArrayType);

    for (auto& token : tokenList) {
        jsonTokenList.PushBack(StringRef(token), alloc);
    }

    payload.AddMember(StringRef(EVENT_PAYLOAD_TOKENS_KEY), jsonTokenList, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!payload.Accept(writer)) {
        ACSDK_ERROR(LX("sendBulkEventFailed").m("Could not construct payload."));
        return;
    }

    auto jsonEventString = buildJsonEventString(eventName, EMPTY_DIALOG_REQUEST_ID, buffer.GetString()).second;

    if (isCertified) {
        m_certifiedSender->sendJSONMessage(jsonEventString);
    } else {
        if (!m_isConnected) {
            ACSDK_WARN(LX(__func__).m("Not connected to AVS.  Not sending Event.").d("event details", jsonEventString));
        } else {
            auto request = std::make_shared<MessageRequest>(jsonEventString);
            m_messageSender->sendMessage(request);
        }
    }
}

void AlertsCapabilityAgent::updateAVSWithLocalVolumeChanges(int8_t volume, bool forceUpdate) {
    if (!forceUpdate && volume == m_lastReportedSpeakerSettings.volume) {
        // Current speaker volume corresponds to what AVS has
        ACSDK_DEBUG7(LX("updateAVSWithLocalVolumeChanges").d("Alerts volume already set to this value", volume));
        return;
    }

    m_lastReportedSpeakerSettings.volume = volume;

    rapidjson::Document payload(kObjectType);
    rapidjson::Document::AllocatorType& alloc = payload.GetAllocator();

    payload.AddMember(StringRef(AVS_PAYLOAD_VOLUME_KEY), volume, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!payload.Accept(writer)) {
        ACSDK_ERROR(LX("updateAVSWithLocalVolumeChangesFailed").m("Could not construct payload."));
        return;
    }

    auto jsonEventString =
        buildJsonEventString(ALERT_VOLUME_CHANGED_EVENT_NAME, EMPTY_DIALOG_REQUEST_ID, buffer.GetString()).second;

    m_certifiedSender->sendJSONMessage(jsonEventString);
}

void AlertsCapabilityAgent::sendProcessingDirectiveException(
    const std::shared_ptr<AVSDirective>& directive,
    const std::string& errorMessage) {
    auto unparsedDirective = directive->getUnparsedDirective();

    ACSDK_ERROR(
        LX("sendProcessingDirectiveException").m("Could not parse directive.").m(errorMessage).m(unparsedDirective));

    m_exceptionEncounteredSender->sendExceptionEncountered(
        unparsedDirective, ExceptionErrorType::UNEXPECTED_INFORMATION_RECEIVED, errorMessage);
}

void AlertsCapabilityAgent::acquireChannel() {
    ACSDK_DEBUG9(LX("acquireChannel"));
    auto activity = FocusManagerInterface::Activity::create(
        NAMESPACE, shared_from_this(), std::chrono::milliseconds::zero(), avsCommon::avs::ContentType::MIXABLE);
    m_focusManager->acquireChannel(FocusManagerInterface::ALERT_CHANNEL_NAME, activity);
}

void AlertsCapabilityAgent::releaseChannel() {
    ACSDK_DEBUG9(LX("releaseChannel"));
    if (m_alertScheduler.getFocusState() != FocusState::NONE) {
        m_focusManager->releaseChannel(FocusManagerInterface::ALERT_CHANNEL_NAME, shared_from_this());
    }
}

void AlertsCapabilityAgent::executeHandleDirectiveImmediately(std::shared_ptr<DirectiveInfo> info) {
    ACSDK_DEBUG1(LX("executeHandleDirectiveImmediately"));
    auto& directive = info->directive;

    rapidjson::Document payload;
    payload.Parse(directive->getPayload());

    if (payload.HasParseError()) {
        std::string errorMessage = "Unable to parse payload";
        ACSDK_ERROR(LX("executeHandleDirectiveImmediatelyFailed").m(errorMessage));
        sendProcessingDirectiveException(directive, errorMessage);
        return;
    }

    auto directiveName = directive->getName();
    std::string alertToken;

    if (DIRECTIVE_NAME_SET_ALERT == directiveName) {
        if (handleSetAlert(directive, payload, &alertToken)) {
            sendEvent(SET_ALERT_SUCCEEDED_EVENT_NAME, alertToken, true);
        } else {
            sendEvent(SET_ALERT_FAILED_EVENT_NAME, alertToken, true);
        }
    } else if (DIRECTIVE_NAME_DELETE_ALERT == directiveName) {
        if (handleDeleteAlert(directive, payload, &alertToken)) {
            sendEvent(DELETE_ALERT_SUCCEEDED_EVENT_NAME, alertToken, true);
        } else {
            sendEvent(DELETE_ALERT_FAILED_EVENT_NAME, alertToken, true);
        }
    } else if (DIRECTIVE_NAME_DELETE_ALERTS == directiveName) {
        handleDeleteAlerts(directive, payload);
    } else if (DIRECTIVE_NAME_SET_VOLUME == directiveName) {
        handleSetVolume(directive, payload);
    } else if (DIRECTIVE_NAME_ADJUST_VOLUME == directiveName) {
        handleAdjustVolume(directive, payload);
    } else if (DIRECTIVE_NAME_SET_ALARM_VOLUME_RAMP == directiveName) {
        handleSetAlarmVolumeRamp(directive, payload);
    }
}

void AlertsCapabilityAgent::executeOnConnectionStatusChanged(const Status status, const ChangedReason reason) {
    ACSDK_DEBUG1(LX("executeOnConnectionStatusChanged").d("status", status).d("reason", reason));
    int wasConnected = m_isConnected;
    m_isConnected = (Status::CONNECTED == status);
    if (m_isConnected && !wasConnected) {
        rapidjson::Value offlineStoppedAlerts(rapidjson::kArrayType);
        rapidjson::Document jsonDoc;
        jsonDoc.SetObject();
        rapidjson::Document::AllocatorType& allocator = jsonDoc.GetAllocator();
        if (m_alertScheduler.getOfflineStoppedAlerts(&offlineStoppedAlerts, allocator)) {
            for (rapidjson::Value::ConstValueIterator itr = offlineStoppedAlerts.Begin();
                 itr != offlineStoppedAlerts.End();
                 ++itr) {
                std::string token = (*itr)[OFFLINE_STOPPED_ALERT_TOKEN_KEY].GetString();
                std::string scheduledTime = (*itr)[OFFLINE_STOPPED_ALERT_SCHEDULED_TIME_KEY].GetString();
                std::string eventTime = (*itr)[OFFLINE_STOPPED_ALERT_EVENT_TIME_KEY].GetString();
                int id = (*itr)[OFFLINE_STOPPED_ALERT_ID_KEY].GetInt();
                sendEvent(ALERT_STOPPED_EVENT_NAME, token, true, scheduledTime, eventTime);
                m_alertScheduler.deleteOfflineStoppedAlert(token, id);
            }
        }
    }
}

void AlertsCapabilityAgent::executeOnAlertStateChange(const AlertObserverInterface::AlertInfo& alertInfo) {
    ACSDK_INFO(LX("executeOnAlertStateChange").d("state", alertInfo.state).d("reason", alertInfo.reason));
    ACSDK_DEBUG1(LX("executeOnAlertStateChange").d("alertToken", alertInfo.token));

    bool alertIsActive = false;
    int alertVolume;

    switch (alertInfo.state) {
        case AlertObserverInterface::State::READY:
            acquireChannel();
            break;

        case AlertObserverInterface::State::STARTED:
            sendEvent(ALERT_STARTED_EVENT_NAME, alertInfo.token, true, alertInfo.reason, currentISO8601TimeUTC());
            alertVolume = getAlertVolume();
            if ((alertVolume != -1) && (alertVolume < ALERT_VOLUME_METRIC_LIMIT)) {
                submitMetric(m_metricRecorder, ALERT_RINGING_LESS_THAN_30_PERCENT_MAX_VOLUME, 1);
                if (alertVolume == 0) {
                    submitMetric(m_metricRecorder, ALERT_RINGING_ZERO_VOLUME, 1);
                }
            }
            submitAlertStartedMetricWithMetadata(alertInfo.token, AlertObserverInterface::typeToString(alertInfo.type));
            updateContextManager();
            alertIsActive = true;
            break;

        case AlertObserverInterface::State::SNOOZED:
            releaseChannel();
            updateContextManager();
            break;

        case AlertObserverInterface::State::STOPPED:
            sendEvent(ALERT_STOPPED_EVENT_NAME, alertInfo.token, true, alertInfo.reason, currentISO8601TimeUTC());
            releaseChannel();
            updateContextManager();
            break;

        case AlertObserverInterface::State::COMPLETED:
            sendEvent(ALERT_STOPPED_EVENT_NAME, alertInfo.token, true, alertInfo.reason, currentISO8601TimeUTC());
            releaseChannel();
            updateContextManager();
            break;

        case AlertObserverInterface::State::ERROR:
            releaseChannel();
            updateContextManager();
            break;

        case AlertObserverInterface::State::PAST_DUE:
            sendEvent(ALERT_STOPPED_EVENT_NAME, alertInfo.token, true, alertInfo.reason, currentISO8601TimeUTC());
            submitAlertCanceledMetricWithMetadata(
                alertInfo.token, AlertObserverInterface::typeToString(alertInfo.type), alertInfo.reason);
            break;

        case AlertObserverInterface::State::FOCUS_ENTERED_FOREGROUND:
            alertIsActive = true;
            sendEvent(ALERT_ENTERED_FOREGROUND_EVENT_NAME, alertInfo.token);
            break;

        case AlertObserverInterface::State::FOCUS_ENTERED_BACKGROUND:
            alertIsActive = true;
            sendEvent(ALERT_ENTERED_BACKGROUND_EVENT_NAME, alertInfo.token);
            break;
        case AlertObserverInterface::State::SCHEDULED_FOR_LATER:
        case AlertObserverInterface::State::DELETED:
            break;
    }

    if (alertIsActive) {
        // Alert is going to go off
        m_alertIsSounding = true;
        // Check if there are lower channels with content being played and increase alert volume if needed.
        if (m_contentChannelIsActive || m_commsChannelIsActive) {
            SpeakerInterface::SpeakerSettings contentSpeakerSettings;
            if (getSpeakerVolumeSettings(&contentSpeakerSettings)) {
                if (m_lastReportedSpeakerSettings.volume < contentSpeakerSettings.volume) {
                    // Adjust alerts volume to be at least as loud as content volume
                    // Do not send a volumeChanged event
                    m_speakerManager->setVolume(
                        ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME,
                        contentSpeakerSettings.volume,
                        SpeakerManagerInterface::NotificationProperties(
                            SpeakerManagerObserverInterface::Source::DIRECTIVE, false, false));
                }
            }
        }
    } else {
        if (m_alertIsSounding) {
            // Alert has just switched from STARTED to something else, since it could not transition from STARTED to
            // READY we may treat it as it is stopping.

            // Reset Active Alerts Volume volume to the Base Alerts Volume when alert stops
            m_alertIsSounding = false;
            m_speakerManager->setVolume(
                ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME,
                m_lastReportedSpeakerSettings.volume,
                SpeakerManagerInterface::NotificationProperties(
                    SpeakerManagerObserverInterface::Source::LOCAL_API, false, false));
        }
    }

    m_executor.submit([this, alertInfo]() { executeNotifyObservers(alertInfo); });
}

void AlertsCapabilityAgent::executeAddObserver(std::shared_ptr<AlertObserverInterface> observer) {
    ACSDK_DEBUG1(LX("executeAddObserver").d("observer", observer.get()));
    m_observers.insert(observer);
}

void AlertsCapabilityAgent::executeRemoveObserver(std::shared_ptr<AlertObserverInterface> observer) {
    ACSDK_DEBUG1(LX("executeRemoveObserver").d("observer", observer.get()));
    m_observers.erase(observer);
}

void AlertsCapabilityAgent::executeNotifyObservers(const AlertObserverInterface::AlertInfo& alertInfo) {
    ACSDK_DEBUG1(LX("executeNotifyObservers")
                     .d("alertToken", alertInfo.token)
                     .d("alertType", alertInfo.type)
                     .d("state", alertInfo.state)
                     .d("reason", alertInfo.reason));
    for (auto observer : m_observers) {
        observer->onAlertStateChange(alertInfo);
    }
}

void AlertsCapabilityAgent::executeRemoveAllAlerts() {
    ACSDK_DEBUG1(LX("executeRemoveAllAlerts"));
    m_alertScheduler.clearData();
}

void AlertsCapabilityAgent::executeOnLocalStop() {
    ACSDK_DEBUG1(LX("executeOnLocalStop"));
    m_alertScheduler.onLocalStop();
}

void AlertsCapabilityAgent::updateContextManager() {
    std::string contextString = getContextString();

    NamespaceAndName namespaceAndName{AVS_CONTEXT_HEADER_NAMESPACE_VALUE_KEY, AVS_CONTEXT_HEADER_NAME_VALUE_KEY};

    auto setStateSuccess = m_contextManager->setState(namespaceAndName, contextString, StateRefreshPolicy::NEVER);

    if (setStateSuccess != SetStateResult::SUCCESS) {
        ACSDK_ERROR(LX("updateContextManagerFailed")
                        .m("Could not set the state on the contextManager")
                        .d("result", static_cast<int>(setStateSuccess)));
    }
}

std::string AlertsCapabilityAgent::getContextString() {
    rapidjson::Document state(kObjectType);
    rapidjson::Document::AllocatorType& alloc = state.GetAllocator();

    auto alertsContextInfo = m_alertScheduler.getContextInfo();
    auto allAlertsJsonValue = buildAllAlertsContext(alertsContextInfo.scheduledAlerts, alloc);
    auto activeAlertsJsonValue = buildActiveAlertsContext(alertsContextInfo.activeAlerts, alloc);

    state.AddMember(StringRef(AVS_CONTEXT_ALL_ALERTS_TOKEN_KEY), allAlertsJsonValue, alloc);
    state.AddMember(StringRef(AVS_CONTEXT_ACTIVE_ALERTS_TOKEN_KEY), activeAlertsJsonValue, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!state.Accept(writer)) {
        ACSDK_ERROR(LX("getContextStringFailed").d("reason", "writerRefusedJsonObject"));
        return "";
    }

    return buffer.GetString();
}

void AlertsCapabilityAgent::clearData() {
    auto result = m_executor.submit([this]() { m_alertScheduler.clearData(Alert::StopReason::LOG_OUT); });
    result.wait();
}

std::unordered_set<std::shared_ptr<avsCommon::avs::CapabilityConfiguration>> AlertsCapabilityAgent::
    getCapabilityConfigurations() {
    return m_capabilityConfigurations;
}

void AlertsCapabilityAgent::onSpeakerSettingsChanged(
    const SpeakerManagerObserverInterface::Source& source,
    const ChannelVolumeInterface::Type& type,
    const SpeakerInterface::SpeakerSettings& settings) {
    m_executor.submit([this, settings, type]() { executeOnSpeakerSettingsChanged(type, settings); });
}

void AlertsCapabilityAgent::onSystemClockSynchronized() {
    m_alertScheduler.reloadAlertsFromDatabase(m_settingsManager, true);
}

bool AlertsCapabilityAgent::getAlertVolumeSettings(SpeakerInterface::SpeakerSettings* speakerSettings) {
    if (!m_speakerManager->getSpeakerSettings(ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME, speakerSettings).get()) {
        ACSDK_ERROR(LX("getAlertSpeakerSettingsFailed").d("reason", "Failed to get speaker settings"));
        return false;
    }
    return true;
}

bool AlertsCapabilityAgent::getSpeakerVolumeSettings(SpeakerInterface::SpeakerSettings* speakerSettings) {
    if (!m_speakerManager->getSpeakerSettings(ChannelVolumeInterface::Type::AVS_SPEAKER_VOLUME, speakerSettings)
             .get()) {
        ACSDK_ERROR(LX("getContentSpeakerSettingsFailed").d("reason", "Failed to get speaker settings"));
        return false;
    }
    return true;
}

void AlertsCapabilityAgent::setNextAlertVolume(int64_t volume) {
    if (volume < speakerConstants::AVS_SET_VOLUME_MIN) {
        volume = speakerConstants::AVS_SET_VOLUME_MIN;
        ACSDK_DEBUG7(LX(__func__).m("Requested volume is lower than allowed minimum, using minimum instead."));
    } else if (volume > speakerConstants::AVS_SET_VOLUME_MAX) {
        volume = speakerConstants::AVS_SET_VOLUME_MAX;
        ACSDK_DEBUG7(LX(__func__).m("Requested volume is higher than allowed maximum, using maximum instead."));
    }

    ACSDK_DEBUG5(LX(__func__).d("New Alerts volume", volume));

    m_speakerManager
        ->setVolume(
            ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME,
            static_cast<int8_t>(volume),
            SpeakerManagerInterface::NotificationProperties(SpeakerManagerObserverInterface::Source::DIRECTIVE))
        .get();

    // Always notify AVS of volume changes here
    updateAVSWithLocalVolumeChanges(static_cast<int8_t>(volume), true);
}

void AlertsCapabilityAgent::submitAlertStartedMetricWithMetadata(
    const std::string& alertToken,
    const std::string& alertType) {
    std::unordered_map<std::string, std::string> metricMetadata;
    std::string ascending =
        ((m_alarmVolumeRampSetting->get() == types::AlarmVolumeRampTypes::ASCENDING) ? "true" : "false");
    long monotonicTime = ((std::clock() - m_startSystemClock) / (double)CLOCKS_PER_SEC) * MILLISECONDS_IN_A_SECOND;
    std::shared_ptr<Alert> alert = m_alertScheduler.getActiveAlert();
    addGenericMetadata(
        metricMetadata,
        alertToken,
        alertType,
        m_isConnected,
        monotonicTime,
        ((alert == nullptr) ? "" : alert->getScheduledTime_ISO_8601()),
        currentISO8601TimeUTC());
    addAlertStartedRingingMetadata(metricMetadata, ascending, getAlertVolume());
    submitMetricWithMetadata(m_metricRecorder, ALERT_STARTED_METRIC_NAME, metricMetadata);
}

void AlertsCapabilityAgent::submitAlertCanceledMetricWithMetadata(
    const std::string& alertToken,
    const std::string& alertType,
    const std::string& scheduledTime) {
    std::unordered_map<std::string, std::string> metricMetadata;
    long monotonicTime = ((std::clock() - m_startSystemClock) / (double)CLOCKS_PER_SEC) * MILLISECONDS_IN_A_SECOND;
    addGenericMetadata(
        metricMetadata, alertToken, alertType, m_isConnected, monotonicTime, scheduledTime, currentISO8601TimeUTC());
    addAlertCanceledMetadata(metricMetadata);
    submitMetricWithMetadata(m_metricRecorder, ALERT_CANCELED_METRIC_NAME, metricMetadata);
}

void AlertsCapabilityAgent::executeOnSpeakerSettingsChanged(
    const ChannelVolumeInterface::Type& type,
    const SpeakerInterface::SpeakerSettings& speakerSettings) {
    if (ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME == type && !m_alertIsSounding) {
        updateAVSWithLocalVolumeChanges(speakerSettings.volume, true);
    }
}

}  // namespace acsdkAlerts
}  // namespace alexaClientSDK
