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

#include <chrono>
#include <thread>

#include <AVSCommon/Utils/Configuration/ConfigurationNode.h>
#include <AVSCommon/Utils/Logger/Logger.h>
#include "SampleApp/PortAudioMicrophoneWrapper.h"
#include "SampleApp/ConsolePrinter.h"

namespace alexaClientSDK {
namespace sampleApp {

using avsCommon::avs::AudioInputStream;

static const std::string SAMPLE_APP_CONFIG_ROOT_KEY("sampleApp");
static const std::string PORTAUDIO_CONFIG_ROOT_KEY("portAudio");

/// String to identify log entries originating from this file.
static const std::string TAG("PortAudioMicrophoneWrapper");

//CURL REQUEST
static const std::string ALEXA_USER_ID("");
static const std::string SKILL_MESSAGING_TOKEN(""); 
static const std::string DATA_MESSAGE("");    //='{"data":{ "sampleMessage": "Sample Message"}, "expiresAfterSeconds": 60}'
static const std::string AUTHORIZATION_HEADER("Authorization: Bearer " + SKILL_MESSAGING_TOKEN);
static const std::string CONTENT_TYPE_HEADER("Content-Type: application/json");
static const std::string URL("https://api.eu.amazonalexa.com/v1/skillmessages/users/" + ALEXA_USER_ID); 

int16_t *new_frame[320];


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
}

bool PortAudioMicrophoneWrapper::initialize() {
    m_writer = m_audioInputStream->createWriter(AudioInputStream::Writer::Policy::NONBLOCKABLE);
    if (!m_writer) {
        ACSDK_CRITICAL(LX("Failed to create stream writer"));
        return false;
    }

    return true;
}

bool PortAudioMicrophoneWrapper::startStreamingMicrophoneData() {
    ACSDK_INFO(LX(__func__));
    std::lock_guard<std::mutex> lock{m_mutex};

    std::thread t1(fillAudioBuffer, this);
    t1.detach();

    m_isStreaming = true;
    m_isActive = true;

    return true;
}

bool PortAudioMicrophoneWrapper::stopStreamingMicrophoneData() {
    ACSDK_INFO(LX(__func__));
    std::lock_guard<std::mutex> lock{m_mutex};
     
    m_isStreaming = false;
    m_isActive = false;
    return true;
}

void PortAudioMicrophoneWrapper::startActivity() {
    m_isActive = true;
}

void PortAudioMicrophoneWrapper::stopActivity() {
    m_isActive = false;
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
    return;
}

int PortAudioMicrophoneWrapper::newAudioFrame(uint8_t* audio) {
    memcpy(new_frame, audio, sizeof(uint16_t)*320);
    ssize_t returnCode = m_writer->write(audio, 320); 
    if (returnCode <= 0) {
        ACSDK_CRITICAL(LX("Failed to write audio frame to stream."));
    }
    return returnCode;
}

void PortAudioMicrophoneWrapper::fillAudioBuffer(void* userData) {
    PortAudioMicrophoneWrapper* wrapper = static_cast<PortAudioMicrophoneWrapper*>(userData);
    int16_t *payload[320];

    while(wrapper->m_isStreaming) {
        while(wrapper->m_isActive); // FIXME: Is there a need for an active wait here?
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ACSDK_INFO(LX("Filling buffer."));

        memset(payload, 0, sizeof(uint16_t)*320);
        ssize_t returnCode = wrapper->m_writer->write(payload, 320); 
        if (returnCode <= 0) {
            ACSDK_CRITICAL(LX("Failed to write blanks to stream."));
        }
    }
}


}  // namespace sampleApp
}  // namespace alexaClientSDK
