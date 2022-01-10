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

#ifndef ALEXA_CLIENT_SDK_CAPABILITYAGENTS_SPEAKERMANAGER_INCLUDE_SPEAKERMANAGER_SPEAKERMANAGERSTORAGESTATE_H_
#define ALEXA_CLIENT_SDK_CAPABILITYAGENTS_SPEAKERMANAGER_INCLUDE_SPEAKERMANAGER_SPEAKERMANAGERSTORAGESTATE_H_

#include <cstdint>

namespace alexaClientSDK {
namespace capabilityAgents {
namespace speakerManager {

/**
 * Storage state for SpeakerManager. SpeakerManager configuration includes configuration for two channel types: speaker
 * and alerts. There can be any number of channels for each of types, but all of them share the same configuraiton.
 */
struct SpeakerManagerStorageState {
    /**
     * SpeakerManager channel type configuration state.
     */
    struct ChannelState {
        /// Channel volume.
        uint8_t channelVolume;
        /// Channel mute status.
        bool channelMuteStatus;
    };
    /// Configuration for speaker channels.
    ChannelState speakerChannelState;
    /// Configuration for alerts channels.
    ChannelState alertsChannelState;
};

}  // namespace speakerManager
}  // namespace capabilityAgents
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_CAPABILITYAGENTS_SPEAKERMANAGER_INCLUDE_SPEAKERMANAGER_SPEAKERMANAGERSTORAGESTATE_H_
