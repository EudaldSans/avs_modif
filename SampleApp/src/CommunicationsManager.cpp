#include <cstring>
#include <string>

#include <rapidjson/document.h>

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <curl/curl.h>
#include <fstream>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include <AVSCommon/Utils/Configuration/ConfigurationNode.h>
#include <AVSCommon/Utils/Logger/Logger.h>
#include "SampleApp/CommunicationsManager.h"
#include "SampleApp/ConsolePrinter.h"

#define MAX_AUDIO_FRAME_SIZE 644
#define ALEXA_SIGNATURE 20

namespace alexaClientSDK {
namespace sampleApp {

/// String to identify log entries originating from this file.
static const std::string TAG("CommunicationsManager");

//UDP SOCKET 
struct sockaddr_in servaddr;
struct sockaddr sender;

socklen_t len;
static const int PORT = 3331;
static const std::string host = "127.0.0.1"; 

int sockfd, dataRecv = 0;
int m_sock;
int16_t *payload[51200];

bool connected = false;
bool isReceiving = false;

bool previous_udp = false;
static const int RIFF_HEADER_SIZE = 44;

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

std::unique_ptr<CommunicationsManager> CommunicationsManager::create(
    std::shared_ptr<InteractionManager> interactionManager,
    std::shared_ptr<PortAudioMicrophoneWrapper> wrapper) {
    if (!wrapper) {
        ACSDK_CRITICAL(LX("Invalid microphone wrapper passed to CommunicationsManager"));
        return nullptr;
    }
    if (!interactionManager) {
        ACSDK_CRITICAL(LX("Invalid InteractionManager passed to UserInputManager"));
        return nullptr;
    }

    return std::unique_ptr<CommunicationsManager> (
        new CommunicationsManager(interactionManager, wrapper));
}

CommunicationsManager::CommunicationsManager(
    std::shared_ptr<InteractionManager> interactionManager,
    std::shared_ptr<PortAudioMicrophoneWrapper> wrapper) :
    m_interactionManager{interactionManager},
    m_wrapper{wrapper},
    m_isStreaming{false} {
}

CommunicationsManager::~CommunicationsManager() {
    close(sockfd);
}

bool CommunicationsManager::initialize() {
    //UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(PORT);

    bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    std::thread t1(receive, this);
    t1.detach();

    return true;
}

void CommunicationsManager::onDialogUXStateChanged(DialogUXState state) {
    if (!connected) return;
    if (state == m_dialogState) return;
    
    m_dialogState = state;
    uint8_t message[5] = {0};
    message[0] = ALEXA_SIGNATURE;
    message[1] = MessageCommand::StateChange;
    
    switch (m_dialogState){
        case DialogUXState::LISTENING:
            message[2] = (uint8_t) DialogUXState::LISTENING;
            ACSDK_CRITICAL(LX("State is listening."));
            send_message(message, 3);
            break;

        case DialogUXState::SPEAKING:
            ACSDK_CRITICAL(LX("State is speaking."));
            message[2] = (uint8_t) DialogUXState::SPEAKING;
            send_message(message, 3);
            break;

        case DialogUXState::IDLE:
            ACSDK_CRITICAL(LX("State is idle."));
            message[2] = (uint8_t) DialogUXState::IDLE;
            send_message(message, 3);
            break;

        case DialogUXState::EXPECTING:
            ACSDK_CRITICAL(LX("State is expecting."));
            message[2] = (uint8_t) DialogUXState::EXPECTING;
            send_message(message, 3);
            return;
        case DialogUXState::THINKING:
            ACSDK_CRITICAL(LX("State is thinking."));
            message[2] = (uint8_t) DialogUXState::THINKING;
            send_message(message, 3);
            return;        
        case DialogUXState::FINISHED:
            /*
            * This is an intermediate state after a SPEAK directive is completed. In the case of a speech burst the
            * next SPEAK could kick in or if its the last SPEAK directive ALEXA moves to the IDLE state. So we do
            * nothing for this state.
            */
            ACSDK_CRITICAL(LX("State is finished."));
            message[2] = (uint8_t) DialogUXState::FINISHED;
            send_message(message, 3);
            return;    
    }
}

void CommunicationsManager::receive(void* userData){ 
    CommunicationsManager* comsManager = static_cast<CommunicationsManager*>(userData); 

    uint8_t payload[MAX_AUDIO_FRAME_SIZE*2] = {0}, fragmented_payload[MAX_AUDIO_FRAME_SIZE] = {0};
    uint8_t assistant_signature;
    uint16_t message_length, position, total_length;
    int frames = 0, expected_number_of_frames = 0, remaining_length;
    // uint8_t expected_sequence_number;
    MessageCommand command;
    ssize_t returnCode;

start:
    comsManager->connect();

    memset(payload, 0, MAX_AUDIO_FRAME_SIZE);
    message_length = 0;
    remaining_length = 0;

    while(1){


        dataRecv = recvfrom(m_sock, payload, MAX_AUDIO_FRAME_SIZE, 0, &sender, &len);
        if (dataRecv <= 0) {
            comsManager->disconnect();
            goto start;
        }

        total_length = dataRecv;
        position = dataRecv;

process_new_packet:        

        assistant_signature = payload[0];
        command = static_cast<MessageCommand>(payload[1]);
        message_length = (payload[2] << 8) | (payload[3]);

        remaining_length = message_length - total_length;

        ACSDK_INFO(LX("Processing new command").d("command", command).d("message_length", message_length));

        while (remaining_length > 0) {
            ACSDK_INFO(LX("Received a fragmented packet").d("message_length", message_length).d("remaining_length", remaining_length));
            memset(fragmented_payload, 0, MAX_AUDIO_FRAME_SIZE);
            dataRecv = recvfrom(m_sock, fragmented_payload, MAX_AUDIO_FRAME_SIZE, 0, &sender, &len);
            if (dataRecv <= 0) {
                comsManager->disconnect();
                goto start;
            }

            memcpy(&payload[position], fragmented_payload, dataRecv);
            remaining_length = remaining_length - dataRecv;
            position = position + dataRecv;
            total_length = total_length + dataRecv;
        }

        if (assistant_signature != ALEXA_SIGNATURE) {
            ACSDK_WARN(LX("Received signature for another assistant").d("signature", assistant_signature));
            continue;
        }

        // ACSDK_INFO(LX("New message").d("command", command).d("len", dataRecv));
        switch (command) {
            
            case MessageCommand::AudioIncoming:
                ACSDK_INFO(LX("Incoming audio."));

                frames = 0;

                if (!comsManager->m_wrapper->isStreaming())  {
                    comsManager->m_interactionManager->tap();
                }
                // expected_sequence_number = 0;
                isReceiving = true;

                comsManager->m_wrapper->startActivity();

                break;

            case MessageCommand::AudioFinished:
                expected_number_of_frames = payload[4] << 8 | payload[5];
                ACSDK_INFO(LX("Finished receiving audio.").d("frames_received", frames).d("expected_frames", expected_number_of_frames));
                isReceiving = false;
                comsManager->m_wrapper->stopActivity();

                break;
            
            case MessageCommand::AudioFrame: // TODO: Request for lost messages?
                ACSDK_INFO(LX("Received new audio."));
                frames++;
                returnCode = 1;
                returnCode = comsManager->m_wrapper->newAudioFrame(&payload[4]);
                if (returnCode <= 0) {
                    ACSDK_CRITICAL(LX("Failed to write audio to stream."));
                }

                break;

            case MessageCommand::StateChange:
                // Should never happen.
                break;   
            default:
                ACSDK_INFO(LX("Got to default").d("command", command));
        }

        if (total_length > message_length) {
            ACSDK_INFO(LX("Received more than one packet").d("message_length", message_length).d("total_length", total_length));
            memmove(payload, &payload[message_length], MAX_AUDIO_FRAME_SIZE - message_length);

            total_length = total_length - message_length;
            goto process_new_packet;
        }
    }   
}

void CommunicationsManager::report(const char* msg, int terminate) {
  perror(msg);
  if (terminate) exit(-1); /* failure */
}

int CommunicationsManager::disconnect() {
    ACSDK_INFO(LX("Disconecting"));
    close(m_sock); /* close the connection */
    connected = false;
    return 0;
}

int CommunicationsManager::connect() {
    // Setup the msock
    sockaddr_in m_addr;
    while (!connected) {
        sleep(1);
        memset(&m_addr, 0, sizeof(m_addr));
        m_sock = socket(AF_INET, SOCK_STREAM, 0);

        int on = 1;
        if (setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &on, sizeof(on)) == -1) {
            std::cout << "Could not set up socket " << std::endl;
            continue;
        }

        // Connect //
        m_addr.sin_family = AF_INET;
        m_addr.sin_port = htons(PORT);
        int status = inet_pton(AF_INET, host.c_str(), &m_addr.sin_addr);

        if (errno == EAFNOSUPPORT) {
            std::cout << "Failed with errno: " << errno << std::endl;
            continue;
        }
        status = ::connect(m_sock, (sockaddr *) &m_addr, sizeof(m_addr));
        if (status == -1) {
            continue;
        }

        connected = true;
    }

    ACSDK_INFO(LX("Connected."));

    return true;
}

int CommunicationsManager::send_message(uint8_t *data, size_t length) {
    ACSDK_INFO(LX("Sending message to server"));
    return ::send(m_sock, data, length, 0);
}

}  // namespace sampleApp
}  // namespace alexaClientSDK