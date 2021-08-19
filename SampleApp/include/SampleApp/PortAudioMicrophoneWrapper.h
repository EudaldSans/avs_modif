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

#ifndef ALEXA_CLIENT_SDK_SAMPLEAPP_INCLUDE_SAMPLEAPP_PORTAUDIOMICROPHONEWRAPPER_H_
#define ALEXA_CLIENT_SDK_SAMPLEAPP_INCLUDE_SAMPLEAPP_PORTAUDIOMICROPHONEWRAPPER_H_

#include <mutex>
#include <thread>

#include <AVSCommon/AVS/AudioInputStream.h>
#include <AVSCommon/SDKInterfaces/DialogUXStateObserverInterface.h>
#include <Audio/MicrophoneInterface.h>

namespace alexaClientSDK {
namespace sampleApp {

enum MessageCommand {AudioIncoming, AudioFinished, AudioFrame, StateChange};

/// Audio Input.
class PortAudioMicrophoneWrapper 
        : public applicationUtilities::resources::audio::MicrophoneInterface
        , public avsCommon::sdkInterfaces::DialogUXStateObserverInterface {
public:
    /**
     * Creates a @c PortAudioMicrophoneWrapper.
     *
     * @param stream The shared data stream to write to.
     * @return A unique_ptr to a @c PortAudioMicrophoneWrapper if creation was successful and @c nullptr otherwise.
     */
    static std::unique_ptr<PortAudioMicrophoneWrapper> create(std::shared_ptr<avsCommon::avs::AudioInputStream> stream);

    /**
     * Stops streaming from the microphone.
     *
     * @return Whether the stop was successful.
     */
    bool stopStreamingMicrophoneData() override;

    /**
     * Starts streaming from the microphone.
     *
     * @return Whether the start was successful.
     */
    bool startStreamingMicrophoneData() override;

    /**
     * Whether the microphone is currently streaming.
     *
     * @return Whether the microphone is streaming.
     */
    bool isStreaming() override;

    /// @name DialogUxStateObserverInetrface methods.
    /// @{
    void onDialogUXStateChanged(DialogUXState state) override;
    /// @}

    /**
     * Destructor.
     */
    ~PortAudioMicrophoneWrapper();

    /**
     * Run the handler.
     *
     * @return Error code.
     */
    int run();

private:
    /**
     * Constructor.
     *
     * @param stream The shared data stream to write to.
     */
    PortAudioMicrophoneWrapper(std::shared_ptr<avsCommon::avs::AudioInputStream> stream);

    /// Initializes 
    bool initialize();

    static void receive(void* userData);

    static void fillAudioBuffer(void* userData);


    static std::vector<int16_t> readAudioFromWav(const std::string& fileName, const int& headerPosition, bool* errorOccurred);

    /// The stream of audio data.
    const std::shared_ptr<avsCommon::avs::AudioInputStream> m_audioInputStream;

    /// The writer that will be used to writer audio data into the sds.
    std::shared_ptr<avsCommon::avs::AudioInputStream::Writer> m_writer;

    /**
     * A lock to seralize access to startStreamingMicrophoneData() and stopStreamingMicrophoneData() between different
     * threads.
     */
    std::mutex m_mutex;

    /**
     * Whether the microphone is currently streaming.
     */
    bool m_isStreaming;

    /**
     * Method to report errors.
     *
     * @param msg Error description.
     * @param terminate Decision to terminate execution.
     */
    void report(const char* msg, int terminate);

    /**
     * Sends messages through the open socket.
     *
     * @param data Message to be sent.
     * @param length Length of message in bytes.
     */
    int send_message(uint8_t *data, size_t length);

    /**
     * Disconect from VAD server.
     */
    int disconnect();

    /**
     * Connect to VAD server.
     */
    int connect();

    /// The current dialog UX state of the SDK
    DialogUXState m_dialogState;
};

}  // namespace sampleApp
}  // namespace alexaClientSDK

#endif  // ALEXA_CLIENT_SDK_SAMPLEAPP_INCLUDE_SAMPLEAPP_PORTAUDIOMICROPHONEWRAPPER_H_