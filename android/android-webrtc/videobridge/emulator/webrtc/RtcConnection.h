// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <api/peer_connection_interface.h>  // for PeerC...
#include <api/scoped_refptr.h>              // for scope...
#include <rtc_base/thread.h>                // for Thread
#include <memory>                           // for uniqu...
#include <nlohmann/json.hpp>                // for json
#include <string>                           // for string

#include "android/base/async/DefaultLooper.h"                   // for Defau...
#include "android/base/async/RecurrentTask.h"                   // for Recur...
#include "emulator/webrtc/capture/GoldfishAudioDeviceModule.h"  // for Goldf...

namespace emulator {
namespace webrtc {

class EmulatorGrpcClient;
using json = nlohmann::json;
using android::base::Looper;
using android::base::RecurrentTask;

// An RtcConnection represents a connection of a webrtc endpoint.
// The RtcConnection is used by a peerconnection to send messages
// to another peerconnection.
class RtcConnection {
public:
    RtcConnection(EmulatorGrpcClient* client);
    ~RtcConnection();

    // Called when a participant is unable to continue the rtc stream.
    // The participant will no longer be in use and close can be called.
    virtual void rtcConnectionDropped(std::string participant) = 0;

    // The connection has actually closed, and can be properly garbage
    // collected.
    virtual void rtcConnectionClosed(std::string participant) = 0;

    // Send a jsep json message to the participant identified by to.
    virtual void send(std::string to, json msg) = 0;

    Looper* getLooper() { return &mLooper; }
    EmulatorGrpcClient* getEmulatorClient() { return mClient; }
    ::webrtc::PeerConnectionFactoryInterface* getPeerConnectionFactory() {
        return mConnectionFactory.get();
    }

protected:
private:
    EmulatorGrpcClient* mClient;

    GoldfishAudioDeviceModule mGoldfishAdm;

    // PeerConnection factory and threads.
    rtc::scoped_refptr<::webrtc::PeerConnectionFactoryInterface>
            mConnectionFactory;
    std::unique_ptr<::webrtc::TaskQueueFactory> mTaskFactory;
    std::unique_ptr<rtc::Thread> mWorker;
    std::unique_ptr<rtc::Thread> mSignaling;
    std::unique_ptr<rtc::Thread> mNetwork;

    // Looper
    android::base::DefaultLooper mLooper;
    std::unique_ptr<std::thread> mLooperThread{nullptr};
    bool mLooperActive{true};
    RecurrentTask mKeepAlive;
};
}  // namespace webrtc
}  // namespace emulator
