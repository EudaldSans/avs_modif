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

#ifndef ALEXA_CLIENT_SDK_ACSDKALERTS_INCLUDE_ACSDKALERTS_ALERT_H_
#define ALEXA_CLIENT_SDK_ACSDKALERTS_INCLUDE_ACSDKALERTS_ALERT_H_

#include "acsdkAlerts/Renderer/Renderer.h"
#include "acsdkAlerts/Renderer/RendererObserverInterface.h"

#include <AVSCommon/AVS/FocusState.h>
#include <AVSCommon/AVS/MixingBehavior.h>
#include <AVSCommon/Utils/Timing/Timer.h>
#include <AVSCommon/Utils/Timing/TimePoint.h>
#include <AVSCommon/Utils/Timing/TimeUtils.h>
#include <acsdkAlertsInterfaces/AlertObserverInterface.h>

#include <Settings/DeviceSettingsManager.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rapidjson/document.h>

namespace alexaClientSDK {
namespace acsdkAlerts {

/**
 * A class to manage the concept of an AVS Alert.
 *
 * This class is decoupled from the Renderer, which is set by its owning object.  This class encpasulates and
 * translates all renderer states, so that an owning object need only know if the alert object is 'active', for
 * example, rather than also query rendering state.  An alert object in an 'active' state implies the user
 * perceivable rendering is occurring (whether that means audible, visual, or other perceivable stimulus).
 */
class Alert
        : public renderer::RendererObserverInterface
        , public std::enable_shared_from_this<Alert> {
public:
    /**
     * An enum class which captures the state an alert object can be in.
     */
    enum class State {
        /// An uninitialized value.
        UNSET,
        /// The alert is set and is expected to become active at some point in the future.
        SET,
        /// The alert is ready to activate, and is waiting for the channel to be acquired.
        READY,
        /// Rendering has been initiated, but is not yet perceivable from a user's point of view.
        ACTIVATING,
        /// Rendering has been initiated, and is perceivable from a user's point of view.
        ACTIVE,
        /// The alert is active, but has been asked to snooze.
        SNOOZING,
        /// The alert is active, but is now stopping due to user interaction.
        SNOOZED,
        /// The renderer is now stopped due to a snooze request.
        STOPPING,
        /// The alert has stopped in response to user interaction.
        STOPPED,
        /// The alert has completed on its own, without user interaction.
        COMPLETED
    };

    /**
     * An enum class which captures the reasons an alert may have stopped.
     */
    enum class StopReason {
        /// An uninitalized value.
        UNSET,
        /// The alert has been stopped due to a Directive from AVS.
        AVS_STOP,
        /// The alert has been stopped due to a local user action.
        LOCAL_STOP,
        /// The alert is being stopped due to an SDK shutdown operation.
        SHUTDOWN,
        /// Logout customer logged out or deregistered.
        LOG_OUT
    };

    /**
     * Utility structure to represent a custom asset sent from AVS.
     */
    struct Asset {
        /**
         * Default Constructor.
         */
        Asset() = default;

        /**
         * Constructor.
         *
         * @param id The id of the asset.
         * @param url The url of the asset.
         */
        Asset(const std::string& id, const std::string& url) : id{id}, url{url} {
        }

        /// The id of the asset.
        std::string id;
        /// The url of the asset.
        std::string url;
    };

    /**
     * A utility structure to encapsulate the data reflecting custom assets for an alert.
     */
    struct AssetConfiguration {
        /**
         * Constructor.
         */
        AssetConfiguration() : loopPause{std::chrono::milliseconds{0}} {
        }

        /// A map of the custom assets, mapping from asset.id to the asset itself.
        std::unordered_map<std::string, Asset> assets;
        /**
         * A vector of the play order of the asset ids.  AVS sends this list in its SetAlert Directive, and to
         * render the alert, all assets mapping to these ids must be played in sequence.
         */
        std::vector<std::string> assetPlayOrderItems;
        /// The background asset id, if specified by AVS.
        std::string backgroundAssetId;
        /// The pause time, in milliseconds, that should be taken between each loop of asset rendering.
        std::chrono::milliseconds loopPause;
    };

    /**
     * A struct to encapsulate an alert's static data. These data members are not expected to change after
     * initialization. The purpose of this struct is to provide access to data members expected to be persisted
     * on-device.
     */
    struct StaticData {
        /**
         * Constructor.
         */
        StaticData() : dbId{0} {
        }
        /// The AVS token for the alert.
        std::string token;

        /// The database id for the alert.
        int dbId;
    };

    /**
     * A struct to encapsulate an alert's dynamic data. These data members are expected to change.
     * The purpose of this struct is to provide access to data members expected to be persisted on-device.
     */
    struct DynamicData {
        /**
         * Constructor.
         */
        DynamicData() : state{State::SET}, loopCount{0}, hasRenderingFailed{false}, originalTime(""), label("") {
        }
        /// The state of the alert.
        State state;

        /// A TimePoint reflecting the time when the alert should become active.
        avsCommon::utils::timing::TimePoint timePoint;

        /// The number of times the sequence of assets should be rendered.
        int loopCount;

        /// A flag to capture if rendering any of asset urls failed.
        bool hasRenderingFailed;

        /// An attribute representing the local time when the alert was originally set.
        std::string originalTime;

        /// An attribute representing the content of the alert.
        std::string label;

        /// The assets associated with this alert.
        AssetConfiguration assetConfiguration;
    };

    /**
     * An enum class which captures the various JSON parse states which may occur.
     */
    enum class ParseFromJsonStatus {
        /// Parsing was successful.
        OK,
        /// A required property was missing from the JSON.
        MISSING_REQUIRED_PROPERTY,
        /// An invalid value was detected while parsing the JSON.
        INVALID_VALUE
    };

    /**
     * Utility struct to allow us to share Context data that can be sent to AVS representing this alert.
     */
    struct ContextInfo {
        /**
         * Constructor.
         *
         * @param token The AVS token identifying this alert.
         * @param type The type of this alert.
         * @param scheduledTime_ISO_8601 The time, in ISO-8601 format, when this alert should activate.
         */
        ContextInfo(const std::string& token, const std::string& type, const std::string& scheduledTime_ISO_8601) :
                token{token},
                type{type},
                scheduledTime_ISO_8601{scheduledTime_ISO_8601} {
        }

        /// The AVS token identifying this alert.
        std::string token;
        /// The type of this alert.
        std::string type;
        /// The time, in ISO-8601 format, when this alert should activate.
        std::string scheduledTime_ISO_8601;
    };

    /**
     * A utility function to convert an originalTime string to an optional of @c OriginalTime.
     *
     * @param originalTimeStr a string of original time.
     * @return An optional with a valid originalTime string; an empty optional otherwise.
     */
    static avsCommon::utils::Optional<acsdkAlertsInterfaces::AlertObserverInterface::OriginalTime>
    validateOriginalTimeString(const std::string& originalTimeStr);

    /**
     * A utility function to convert a label string to an optional.
     *
     * @param label a string of label.
     * @return An optional with a valid label string; an empty optional otherwise.
     */
    static avsCommon::utils::Optional<std::string> validateLabelString(const std::string& label);

    /**
     * A utility function to convert an alert state enum value to a string.
     *
     * @param state The alert state.
     * @return The string conversion.
     */
    static std::string stateToString(Alert::State state);

    /**
     * A utility function to convert a StopReason enum value to a string.
     *
     * @param stopReason The stop reason.
     * @return The string conversion.
     */
    static std::string stopReasonToString(Alert::StopReason stopReason);

    /**
     * A utility function to convert a ParseFromJsonStatus enum value to a string.
     *
     * @param parseFromJsonStatus The parse status.
     * @return The string conversion.
     */
    static std::string parseFromJsonStatusToString(Alert::ParseFromJsonStatus parseFromJsonStatus);

    /**
     * Constructor.
     */
    Alert(
        std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()>
            defaultAudioFactory,
        std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> shortAudioFactory,
        std::shared_ptr<settings::DeviceSettingsManager> settingsManager);

    /**
     * Returns a string to identify the type of the class.  Required for persistent storage.
     *
     * @return The type name of the alert.
     */
    virtual std::string getTypeName() const = 0;

    /**
     * A function that gets a factory to create a stream to the default audio for an alert.
     *
     * @return A factory function that generates a default audio stream.
     */
    std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()>
    getDefaultAudioFactory() const;

    /**
     * A function that gets a factory to create a stream to the short audio for an alert.
     *
     * @return A factory function that generates a short audio stream.
     */
    std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()> getShortAudioFactory()
        const;

    /**
     * Returns the Context data which may be shared with AVS.
     *
     * @return The Context data which may be shared with AVS.
     */
    Alert::ContextInfo getContextInfo() const;

    void onRendererStateChange(renderer::RendererObserverInterface::State state, const std::string& reason) override;

    /**
     * Given a rapidjson pre-parsed Value, parse the fields required for a valid alert.
     *
     * @param payload The pre-parsed rapidjson::Value.
     * @param[out] errorMessage An output parameter where a parse error message may be stored.
     * @return The status of the parse.
     */
    ParseFromJsonStatus parseFromJson(const rapidjson::Value& payload, std::string* errorMessage);

    /**
     * Set the renderer on the alert.
     *
     * @param renderer The renderer to set on the alert.
     */
    void setRenderer(std::shared_ptr<renderer::RendererInterface> renderer);

    /**
     * Set an observer on the alert.  An alert may have only one observer - repeated calls to this function will
     * replace any previous value with the new one.
     *
     * @param observer The observer to set on this alert.
     */
    void setObserver(acsdkAlertsInterfaces::AlertObserverInterface* observer);

    /**
     * Activate the alert.
     */
    void activate();

    /**
     * Deactivate the alert.
     *
     * @param reason The reason why the alert is being stopped.
     */
    void deactivate(StopReason reason);

    /**
     * Performs relevant operations to update this alarm to the new time provided.
     *
     * @note Use @c snooze for active alarms. This method will fail since it does not stop alarm rendering.
     * @note The caller should validate the new schedule which should not be more than 30 minutes in the past.
     * @param newScheduledTime The new time for the alarm to occur, in ISO-8601 format.
     * @return @c true if it succeeds; @c false otherwise.
     */
    bool updateScheduledTime(const std::string& newScheduledTime);

    /**
     * Performs relevant operations to snooze this alarm to the new time provided.
     *
     * @param updatedScheduledTime The new time for the alarm to occur, in ISO-8601 format.
     * @return @c true if it succeeds; @c false otherwise.
     */
    bool snooze(const std::string& updatedScheduledTime);

    /**
     * Sets the focus state for the alert.
     *
     * @param focusState The focus state.
     * @param behavior The mixing behavior.
     */
    void setFocusState(avsCommon::avs::FocusState focusState, avsCommon::avs::MixingBehavior behavior);

    /**
     * Sets the state of this alert to active.  Only has effect if the Alert's state is State::ACTIVATING.
     */
    bool setStateActive();

    /**
     * Sets the alert back to being set to go off in the future.
     */
    void reset();

    /**
     * Returns the AVS token for the alert.
     *
     * @return The AVS token for the alert.
     */
    std::string getToken() const;

    /**
     * Gets the time the alert should occur, in Unix epoch time.
     *
     * @return The time the alert should occur, in Unix epoch time.
     */
    int64_t getScheduledTime_Unix() const;

    /**
     * Gets the time the alert should occur, in ISO-8601 format.
     *
     * @return The time the alert should occur, in ISO-8601 format.
     */
    std::string getScheduledTime_ISO_8601() const;

    /**
     * Gets the UTC time for when the alert should occur.
     *
     * @return The UTC time for when the alert should occur.
     */
    std::chrono::system_clock::time_point getScheduledTime_Utc_TimePoint() const;

    /**
     * Gets the @c Type of the alert.
     *
     * @return The type of the alert.
     */
    acsdkAlertsInterfaces::AlertObserverInterface::Type getType() const;

    /**
     * Gets the optional with @c OriginalTime.
     *
     * @return An optional with @c OriginalTime if originalTime is valid for the alert; an empty optional otherwise.
     */
    avsCommon::utils::Optional<acsdkAlertsInterfaces::AlertObserverInterface::OriginalTime> getOriginalTime() const;

    /**
     * Gets the optional with label string.
     *
     * @return An optional with label string if label is valid for the alert; an empty optional otherwise.
     */
    avsCommon::utils::Optional<std::string> getLabel() const;

    /**
     * Returns the state of the alert.
     *
     * @return The state of the alert.
     */
    Alert::State getState() const;

    /**
     * Returns the reason the alert stopped.
     *
     * @return The reason the alert stopped.
     */
    StopReason getStopReason() const;

    /**
     * Gets the data associated with this alert.
     * @param dynamicData A pointer to a DynamicData struct. If this is not nullptr, it will be populated with this
     * alert's DynamicData.
     * @param staticData A pointer to a StaticData struct. If this is not nullptr, it will be populated with this
     * alert's StaticData.
     */
    void getAlertData(StaticData* staticData, DynamicData* dynamicData) const;

    /**
     * Sets the data associated with this alert.
     * @param dynamicData A pointer to a DynamicData struct. If this is not nullptr, this alert's DynamicData will be
     * set.
     * @param staticData A pointer to a StaticData struct. If this is not nullptr, this alert's StaticData will be set.
     * @return True if the alert data was successfully set.
     */
    bool setAlertData(StaticData* staticData, DynamicData* dynamicData);

    /**
     * Returns the database id for the alert, if one is set.
     *
     * @return The database id for the alert, if one is set.
     */
    int getId() const;

    /**
     * Queries whether the alert is past-due.
     *
     * @param currentUnixTime The time with which to compare the activation time of the alert.
     * @param timeLimitSeconds How long an alert may be late, and still considered valid.
     * @return If the alert is considered past-due.
     */
    bool isPastDue(int64_t currentUnixTime, std::chrono::seconds timeLimit);

    /**
     * Get the loop count of custom assets.
     *
     * @return The loop count of custom assets.
     */
    int getLoopCount() const;

    /**
     * Get the time, in milliseconds, to be paused between custom-asset loop rendering.
     *
     * @return The time, in milliseconds, to be paused between custom-asset loop rendering.
     */
    std::chrono::milliseconds getLoopPause() const;

    /**
     * Get the background custom asset id, as specified by AVS.
     *
     * @return The background custom asset id, as specified by AVS.
     */

    std::string getBackgroundAssetId() const;

    /**
     * Returns the utility structure, containing the Context data associated with this alert.
     *
     * @return The utility structure, containing the Context data associated with this alert.
     */
    AssetConfiguration getAssetConfiguration() const;

    /**
     * Sets the asset configuration of this alert.
     *
     * @return whether the asset configuration was set.
     */
    bool setAssetConfiguration(const AssetConfiguration& assetConfiguration);

    /**
     * A utility function to print the internals of an alert.
     */
    void printDiagnostic();

private:
    /**
     * Returns the AVS token for the alert.
     *
     * @return The AVS token for the alert.
     */
    std::string getTokenLocked() const;

    /**
     * Returns the alert's scheduled time in Unix.
     *
     * @return The alert's scheduled time in Unix.
     */
    int64_t getScheduledTime_UnixLocked() const;

    /**
     * Returns the alert's scheduled time in ISO 8601.
     *
     * @return The alert's scheduled time in ISO 8601.
     */
    std::string getScheduledTime_ISO_8601Locked() const;

    /**
     * Gets scheduled time point in UTC time. This function should be called with a lock.
     *
     * @return The alert's scheduled time point in UTC time.
     */
    std::chrono::system_clock::time_point getScheduledTime_Utc_TimePointLocked() const;

    /**
     * Gets the optional of the original time. This function should be called with a lock.
     *
     * @return An optional with @c OriginalTime if originalTime is valid for the alert; an empty optional otherwise.
     */
    avsCommon::utils::Optional<acsdkAlertsInterfaces::AlertObserverInterface::OriginalTime> getOriginalTimeLocked()
        const;

    /**
     * Gets the optional of the label string. This function should be called with a lock.
     *
     * @return An optional with label string if label is valid for the alert; an empty optional otherwise.
     */
    avsCommon::utils::Optional<std::string> getLabelLocked() const;

    /**
     * Utility function to begin the alert's renderer in an unlocked context
     */
    void startRenderer();

    /**
     * Utility function to begin the alert's renderer in a locked context
     */
    void startRendererLocked();

    /**
     * A utility function to be invoked when the maximum time for an alert has expired.
     */
    void onMaxTimerExpiration();

    /**
     * Function to determine whether an alert is in a paused state.
     *
     * @return Whether alert is paused
     */
    bool isAlertPaused() const;

    /// The mutex that enforces thread safety for all member variables.
    mutable std::mutex m_mutex;
    /// Persistable Alert attributes that are expected to remain constant.
    StaticData m_staticData;
    /// Persistable Alert attributes that are expected to be readable/writable.
    DynamicData m_dynamicData;
    /// The reason the alert has been stopped.
    StopReason m_stopReason;
    /// The current focus state of the alert.
    avsCommon::avs::FocusState m_focusState;
    /// The current mixing behavior of the alert.
    avsCommon::avs::MixingBehavior m_mixingBehavior;
    /// A flag to capture if the maximum time timer has expired for this alert.
    bool m_hasTimerExpired;
    /// The timer to ensure this alert is not active longer than a maximum length of time.
    avsCommon::utils::timing::Timer m_maxLengthTimer;
    /// The observer of the alert.
    acsdkAlertsInterfaces::AlertObserverInterface* m_observer;
    /// The renderer for the alert.
    std::shared_ptr<renderer::RendererInterface> m_renderer;
    /// This is the factory that provides a default audio stream.
    const std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()>
        m_defaultAudioFactory;
    /// This is the factory that provides a short audio stream.
    const std::function<std::pair<std::unique_ptr<std::istream>, const avsCommon::utils::MediaType>()>
        m_shortAudioFactory;
    /// The settings manager used to retrieve the value of alarm volume ramp setting.
    std::shared_ptr<settings::DeviceSettingsManager> m_settingsManager;
    /// A flag to keep track if focus changed while alert is activating. If true need to restart
    /// renderer with updated focus.
    bool m_focusChangedDuringAlertActivation;
    /// A flag to keep track of whether we should restart the renderer after it fully stops.
    bool m_startRendererAgainAfterFullStop;
};

/**
 * A utility struct which allows alert objects to be sorted uniquely by time in STL containers such as sets.
 */
struct TimeComparator {
    /**
     * Alerts may have the same time stamp, so let's include the token to ensure unique and consistent ordering.
     */
    bool operator()(const std::shared_ptr<Alert>& lhs, const std::shared_ptr<Alert>& rhs) const {
        if (lhs->getScheduledTime_Unix() == rhs->getScheduledTime_Unix()) {
            return (lhs->getToken() < rhs->getToken());
        }

        return (lhs->getScheduledTime_Unix() < rhs->getScheduledTime_Unix());
    }
};

/**
 * Write a @c Alert::State value to an @c ostream as a string.
 *
 * @param stream The stream to write the value to.
 * @param state The @c Alert::State value to write to the @c ostream as a string.
 * @return The @c ostream that was passed in and written to.
 */
inline std::ostream& operator<<(std::ostream& stream, const Alert::State& state) {
    return stream << Alert::stateToString(state);
}

/**
 * Write a @c Alert::StopReason value to an @c ostream as a string.
 *
 * @param stream The stream to write the value to.
 * @param reason The @c Alert::StopReason value to write to the @c ostream as a string.
 * @return The @c ostream that was passed in and written to.
 */
inline std::ostream& operator<<(std::ostream& stream, const Alert::StopReason& reason) {
    return stream << Alert::stopReasonToString(reason);
}

/**
 * Write a @c Alert::ParseFromJsonStatus value to an @c ostream as a string.
 *
 * @param stream The stream to write the value to.
 * @param status The @c Alert::ParseFromJsonStatus value to write to the @c ostream as a string.
 * @return The @c ostream that was passed in and written to.
 */
inline std::ostream& operator<<(std::ostream& stream, const Alert::ParseFromJsonStatus& status) {
    return stream << Alert::parseFromJsonStatusToString(status);
}

}  // namespace acsdkAlerts
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_ACSDKALERTS_INCLUDE_ACSDKALERTS_ALERT_H_
