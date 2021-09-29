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
#include "SampleApp/PortAudioMicrophoneWrapper.h"
#include "SampleApp/ConsolePrinter.h"

#define MAX_AUDIO_FRAME_SIZE 644
#define ALEXA_SIGNATURE 20

namespace alexaClientSDK {
namespace sampleApp {

using avsCommon::avs::AudioInputStream;

static const std::string SAMPLE_APP_CONFIG_ROOT_KEY("sampleApp");
static const std::string PORTAUDIO_CONFIG_ROOT_KEY("portAudio");

/// String to identify log entries originating from this file.
static const std::string TAG("PortAudioMicrophoneWrapper");

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

const char* books[] = {"War and Peace",
                       "Pride and Prejudice",
                       "The Sound and the Fury"};

//FOR SKILL
//Wav's path
static std::string wav_path = "/home/pi/avs-device-sdk/SampleApp/inputs";
// This is a 16 bit 16 kHz little endian linear PCM audio file of "de la cocina".
static const std::string COCINA_AUDIO_FILE = "/Cocina.wav";
// This is a 16 bit 16 kHz little endian linear PCM audio file of "del comedor".
static const std::string COMEDOR_AUDIO_FILE = "/Comedor.wav";

//CURL REQUEST
static const std::string ALEXA_USER_ID("");
static const std::string SKILL_MESSAGING_TOKEN(""); 
static const std::string DATA_MESSAGE("");    //='{"data":{ "sampleMessage": "Sample Message"}, "expiresAfterSeconds": 60}'
static const std::string AUTHORIZATION_HEADER("Authorization: Bearer " + SKILL_MESSAGING_TOKEN);
static const std::string CONTENT_TYPE_HEADER("Content-Type: application/json");
static const std::string URL("https://api.eu.amazonalexa.com/v1/skillmessages/users/" + ALEXA_USER_ID); 

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

std::unique_ptr<PortAudioMicrophoneWrapper> PortAudioMicrophoneWrapper::create(
    std::shared_ptr<AudioInputStream> stream) {
    if (!stream) {
        ACSDK_CRITICAL(LX("Invalid stream passed to PortAudioMicrophoneWrapper"));
        return nullptr;
    }
    std::unique_ptr<PortAudioMicrophoneWrapper> portAudioMicrophoneWrapper(new PortAudioMicrophoneWrapper(stream));
    if (!portAudioMicrophoneWrapper->initialize()) {
        ACSDK_CRITICAL(LX("Failed to initialize PortAudioMicrophoneWrapper"));
        return nullptr;
    }
    return portAudioMicrophoneWrapper;
}

PortAudioMicrophoneWrapper::PortAudioMicrophoneWrapper(std::shared_ptr<AudioInputStream> stream) :
    m_audioInputStream{stream},
    m_isStreaming{false} {
}

PortAudioMicrophoneWrapper::~PortAudioMicrophoneWrapper() {
    close(sockfd);
}

bool PortAudioMicrophoneWrapper::initialize() {
    m_writer = m_audioInputStream->createWriter(AudioInputStream::Writer::Policy::NONBLOCKABLE);
    if (!m_writer) {
        ACSDK_CRITICAL(LX("Failed to create stream writer"));
        return false;
    }

    //UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(PORT);

    bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

    //Curl request
  //  CURL* curl = curl_easy_init();

    return true;
}

bool PortAudioMicrophoneWrapper::startStreamingMicrophoneData() {
    ACSDK_INFO(LX(__func__));
    std::lock_guard<std::mutex> lock{m_mutex};
    
    std::thread t1(receive, this);
    t1.detach();

    std::thread t2(fillAudioBuffer, this);
    t2.detach();

    m_isStreaming = true;

    ACSDK_CRITICAL(LX("START"));

    return true;
}

bool PortAudioMicrophoneWrapper::stopStreamingMicrophoneData() {
    ACSDK_INFO(LX(__func__));
    std::lock_guard<std::mutex> lock{m_mutex};
     
    m_isStreaming = false;
    return true;
}

bool PortAudioMicrophoneWrapper::isStreaming() {
    return m_isStreaming;
}

std::vector<int16_t> PortAudioMicrophoneWrapper::readAudioFromWav(const std::string& fileName, const int& headerPosition, bool* errorOccurred) {
    std::ifstream inputFile(fileName.c_str(), std::ifstream::binary);
    if (!inputFile.good()) {
        std::cout << "Couldn't open audio file!" << std::endl;
        if (errorOccurred) {
            *errorOccurred = true;
        }
        return {};
    }
    inputFile.seekg(0, std::ios::end);
    int fileLengthInBytes = inputFile.tellg();

    if (fileLengthInBytes <= headerPosition) {
        std::cout << "File should be larger than header position" << std::endl;
        if (errorOccurred) {
            *errorOccurred = true;
        }
        return {};
    }

    inputFile.seekg(headerPosition, std::ios::beg);

    int numSamples = (fileLengthInBytes - headerPosition) / sizeof(int16_t);

    std::vector<int16_t> retVal(numSamples, 0);

    inputFile.read((char*)&retVal[0], numSamples * sizeof(int16_t));

    if (static_cast<size_t>(inputFile.gcount()) != numSamples * sizeof(int16_t)) {
        std::cout << "Error reading audio file" << std::endl;
        if (errorOccurred) {
            *errorOccurred = true;
        }
        return {};
    }

    inputFile.close();
    if (errorOccurred) {
        *errorOccurred = false;
    }

    std::cout << "File read successfully" << std::endl;

    return retVal;
}

void PortAudioMicrophoneWrapper::onDialogUXStateChanged(DialogUXState state) {
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

void PortAudioMicrophoneWrapper::fillAudioBuffer(void* userData) {
    PortAudioMicrophoneWrapper* wrapper = static_cast<PortAudioMicrophoneWrapper*>(userData);

    while(wrapper->m_isStreaming) {
        while(isReceiving); // FIXME: Is there a need for an active wait here?
        sleep(1);

        memset(payload, 0, sizeof(uint16_t)*320);
        ssize_t returnCode = wrapper->m_writer->write(payload, 320); 
        if (returnCode <= 0) {
            ACSDK_CRITICAL(LX("Failed to write blanks to stream."));
        }
    }
}

void PortAudioMicrophoneWrapper::receive(void* userData){ 
    PortAudioMicrophoneWrapper* wrapper = static_cast<PortAudioMicrophoneWrapper*>(userData); 

    uint8_t payload[MAX_AUDIO_FRAME_SIZE] = {0}, fragmented_payload[MAX_AUDIO_FRAME_SIZE] = {0};
    uint8_t assistant_signature;
    uint16_t message_length, position, total_length;
    int frames = 0, expected_number_of_frames = 0, remaining_length;
    // uint8_t expected_sequence_number;
    MessageCommand command;
    ssize_t returnCode;

start:
    wrapper->connect();

    memset(payload, 0, MAX_AUDIO_FRAME_SIZE);
    message_length = 0;
    remaining_length = 0;

    while(1){


        dataRecv = recvfrom(m_sock, payload, MAX_AUDIO_FRAME_SIZE, 0, &sender, &len);
        if (dataRecv <= 0) {
            wrapper->disconnect();
            goto start;
        }

        total_length = dataRecv;
        position = dataRecv;

process_new_packet:        

        assistant_signature = payload[0];
        command = static_cast<MessageCommand>(payload[1]);
        message_length = (payload[2] << 8) | (payload[3]);

        remaining_length = message_length - total_length;

        while (remaining_length > 0) {
            ACSDK_INFO(LX("Received a fragmented packet").d("message_length", message_length).d("remaining_length", remaining_length));
            memset(fragmented_payload, 0, MAX_AUDIO_FRAME_SIZE);
            dataRecv = recvfrom(m_sock, fragmented_payload, MAX_AUDIO_FRAME_SIZE, 0, &sender, &len);
            if (dataRecv <= 0) {
                wrapper->disconnect();
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
                // expected_sequence_number = 0;
                isReceiving = true;

                break;

            case MessageCommand::AudioFinished:
                expected_number_of_frames = payload[4] << 8 | payload[5];
                ACSDK_INFO(LX("Finished receiving audio.").d("frames_received", frames).d("expected_frames", expected_number_of_frames));
                isReceiving = false;

                break;
            
            case MessageCommand::AudioFrame: // TODO: Request for lost messages?
                frames++;
                returnCode = wrapper->m_writer->write(&payload[4], 320); 
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

void PortAudioMicrophoneWrapper::report(const char* msg, int terminate) {
  perror(msg);
  if (terminate) exit(-1); /* failure */
}

int PortAudioMicrophoneWrapper::disconnect() {
    ACSDK_INFO(LX("Disconecting"));
    close(m_sock); /* close the connection */
    connected = false;
    return 0;
}

int PortAudioMicrophoneWrapper::connect() {
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

int PortAudioMicrophoneWrapper::send_message(uint8_t *data, size_t length) {
    ACSDK_INFO(LX("Sending message to server"));
    return ::send(m_sock, data, length, 0);
}


}  // namespace sampleApp
}  // namespace alexaClientSDK
