#ifndef ALEXA_CLIENT_SDK_SAMPLEAPP_INCLUDE_SAMPLEAPP_COMMUNICATIONSMANAGER_H_
#define ALEXA_CLIENT_SDK_SAMPLEAPP_INCLUDE_SAMPLEAPP_COMMUNICATIONSMANAGER_H_

#include <mutex>
#include <thread>

#include <AVSCommon/SDKInterfaces/DialogUXStateObserverInterface.h>
#include <SampleApp/PortAudioMicrophoneWrapper.h>

namespace alexaClientSDK {
namespace sampleApp {

enum MessageCommand {AudioIncoming, AudioFinished, AudioFrame, StateChange};

/// Audio Input.
class CommunicationsManager 
        : public avsCommon::sdkInterfaces::DialogUXStateObserverInterface {
public:
    /**
     * Creates a @c CommunicationsManager.
     *
     * @param stream The shared data stream to write to.
     * @return A unique_ptr to a @c CommunicationsManager if creation was successful and @c nullptr otherwise.
     */
    static std::unique_ptr<CommunicationsManager> create(std::shared_ptr<alexaClientSDK::sampleApp::PortAudioMicrophoneWrapper> wrapper);

    /// @name DialogUxStateObserverInetrface methods.
    /// @{
    void onDialogUXStateChanged(DialogUXState state) override;
    /// @}

    /**
     * Destructor.
     */
    ~CommunicationsManager();

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
    CommunicationsManager(std::shared_ptr<alexaClientSDK::sampleApp::PortAudioMicrophoneWrapper> wrapper);

    /// Initializes 
    bool initialize();

    static void receive(void* userData);

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