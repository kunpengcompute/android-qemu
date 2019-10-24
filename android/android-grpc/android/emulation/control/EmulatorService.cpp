// Copyright (C) 2018 The Android Open Source Project
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

#include "android/emulation/control/EmulatorService.h"

#include <grpcpp/grpcpp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "android/base/Log.h"
#include "android/base/Uuid.h"
#include "android/base/system/System.h"
#include "android/console.h"
#include "android/emulation/LogcatPipe.h"
#include "android/emulation/control/RtcBridge.h"
#include "android/emulation/control/ScreenCapturer.h"
#include "android/emulation/control/battery_agent.h"
#include "android/emulation/control/display_agent.h"
#include "android/emulation/control/finger_agent.h"
#include "android/emulation/control/interceptor/LoggingInterceptor.h"
#include "android/emulation/control/interceptor/MetricsInterceptor.h"
#include "android/emulation/control/keyboard/EmulatorKeyEventSender.h"
#include "android/emulation/control/keyboard/TouchEventSender.h"
#include "android/emulation/control/location_agent.h"
#include "android/emulation/control/logcat/RingStreambuf.h"
#include "android/emulation/control/snapshot/CallbackStreambuf.h"
#include "android/emulation/control/snapshot/GzipStreambuf.h"
#include "android/emulation/control/snapshot/TarStream.h"
#include "android/emulation/control/telephony_agent.h"
#include "android/emulation/control/user_event_agent.h"
#include "android/emulation/control/vm_operations.h"
#include "android/emulation/control/waterfall/SocketController.h"
#include "android/emulation/control/waterfall/WaterfallForwarder.h"
#include "android/emulation/control/waterfall/WaterfallServiceLibrary.h"
#include "android/emulation/control/window_agent.h"
#include "android/opengles.h"
#include "android/skin/rect.h"
#include "android/snapshot/PathUtils.h"
#include "android/snapshot/Snapshot.h"
#include "android/utils/path.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller.pb.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_builder_impl.h"
#include "grpcpp/server_impl.h"
#include "waterfall.grpc.pb.h"

namespace google {
namespace protobuf {
class Empty;
}  // namespace protobuf
}  // namespace google
namespace waterfall {
class CmdProgress;
class ForwardMessage;
class Message;
class Transfer;
class VersionMessage;
}  // namespace waterfall

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using namespace android::base;
using namespace android::control::interceptor;

namespace android {
namespace emulation {
namespace control {

class EmulatorControllerServiceImpl : public EmulatorControllerService {
public:
    void stop() override {
        auto deadline = std::chrono::system_clock::now() +
                        std::chrono::milliseconds(500);
        mServer->Shutdown(deadline);
    }

    EmulatorControllerServiceImpl(int port,
                                  EmulatorController::Service* service,
                                  waterfall::Waterfall::Service* waterfall,
                                  grpc::Server* server)
        : mPort(port),
          mService(service),
          mForwarder(waterfall),
          mServer(server) {}

    int port() override { return mPort; }

private:
    std::unique_ptr<EmulatorController::Service> mService;
    std::unique_ptr<waterfall::Waterfall::Service> mForwarder;

    std::unique_ptr<grpc::Server> mServer;
    int mPort;
};

class WaterfallImpl final : public waterfall::Waterfall::Service {
public:
    WaterfallImpl() : mWaterfall(new ControlSocketLibrary()) {}

    Status Forward(ServerContext* context,
                   ::grpc::ServerReaderWriter<::waterfall::ForwardMessage,
                                              ::waterfall::ForwardMessage>*
                           stream) override {
        return StreamingToStreaming<::waterfall::ForwardMessage>(
                mWaterfall.get(), stream,
                [](auto stub, auto ctx) { return stub->Forward(ctx); });
    }

    Status Echo(
            ServerContext* context,
            ::grpc::ServerReaderWriter<::waterfall::Message,
                                       ::waterfall::Message>* stream) override {
        return StreamingToStreaming<::waterfall::Message>(
                mWaterfall.get(), stream,
                [](auto stub, auto ctx) { return stub->Echo(ctx); });
    }

    Status Exec(ServerContext* context,
                ::grpc::ServerReaderWriter<::waterfall::CmdProgress,
                                           ::waterfall::CmdProgress>* stream)
            override {
        return StreamingToStreaming<::waterfall::CmdProgress>(
                mWaterfall.get(), stream,
                [](auto stub, auto ctx) { return stub->Exec(ctx); });
    }

    Status Pull(ServerContext* context,
                const ::waterfall::Transfer* request,
                ServerWriter<::waterfall::Transfer>* writer) override {
        return UnaryToStreaming<::waterfall::Transfer>(
                mWaterfall.get(), writer, [&request](auto stub, auto ctx) {
                    return stub->Pull(ctx, *request);
                });
    }

    Status Push(ServerContext* context,
                grpc::ServerReader<::waterfall::Transfer>* reader,
                ::waterfall::Transfer* reply) override {
        return StreamingToUnary<::waterfall::Transfer>(
                mWaterfall.get(), reader, [&reply](auto stub, auto ctx) {
                    return stub->Push(ctx, reply);
                });
    }

    Status Version(ServerContext* context,
                   const ::google::protobuf::Empty* request,
                   ::waterfall::VersionMessage* reply) override {
        ScopedWaterfallStub fwd(mWaterfall.get());
        if (!fwd.get())
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "Unable to reach waterfall");

        ::grpc::ClientContext defaultCtx;
        return fwd->Version(&defaultCtx, *request, reply);
    }

private:
    std::unique_ptr<WaterfallServiceLibrary> mWaterfall;
};

// Logic and data behind the server's behavior.
class EmulatorControllerImpl final : public EmulatorController::Service {
public:
    EmulatorControllerImpl(const AndroidConsoleAgents* agents,
                           RtcBridge* rtcBridge)
        : mAgents(agents),
          mRtcBridge(rtcBridge),
          mLogcatBuffer(k128KB),
          mKeyEventSender(agents),
          mTouchEventSender(agents) {
        // the logcat pipe will take ownership of the created stream, and writes
        // to our buffer.
        LogcatPipe::registerStream(new std::ostream(&mLogcatBuffer));
    }

    Status getLogcat(ServerContext* context,
                     const LogMessage* request,
                     LogMessage* reply) override {
        auto message = mLogcatBuffer.bufferAtOffset(request->start(), kNoWait);
        reply->set_start(message.first);
        reply->set_contents(message.second);
        reply->set_next(message.first + message.second.size());
        return Status::OK;
    }

    Status streamLogcat(ServerContext* context,
                        const LogMessage* request,
                        ServerWriter<LogMessage>* writer) override {
        LogMessage log;
        log.set_next(request->start());
        do {
            // When streaming, block at most 5 seconds before sending any status
            // This also makes sure we check that the clients is still around at
            // least once every 5 seconds.
            auto message =
                    mLogcatBuffer.bufferAtOffset(log.next(), k5SecondsWait);
            log.set_start(message.first);
            log.set_contents(message.second);
            log.set_next(message.first + message.second.size());
        } while (writer->Write(log));
        return Status::OK;
    }

    Status setRotation(ServerContext* context,
                       const Rotation* request,
                       Rotation* reply) override {
        mAgents->emu->rotate((SkinRotation)request->rotation());
        return getRotation(context, nullptr, reply);
        return Status::OK;
    }

    Status getRotation(ServerContext* context,
                       const ::google::protobuf::Empty* request,
                       Rotation* reply) override {
        reply->set_rotation((Rotation_SkinRotation)mAgents->emu->getRotation());
        return Status::OK;
    }

    Status setBattery(ServerContext* context,
                      const BatteryState* request,
                      BatteryState* reply) override {
        auto battery = mAgents->battery;
        battery->setHasBattery(request->hasbattery());
        battery->setIsBatteryPresent(request->ispresent());
        battery->setIsCharging(
                request->status() ==
                BatteryState_BatteryStatus_BATTERY_STATUS_CHARGING);
        battery->setCharger((BatteryCharger)request->charger());
        battery->setChargeLevel(request->chargelevel());
        battery->setHealth((BatteryHealth)request->health());
        battery->setStatus((BatteryStatus)request->status());
        return getBattery(context, nullptr, reply);
    }

    Status getBattery(ServerContext* context,
                      const ::google::protobuf::Empty* request,
                      BatteryState* reply) override {
        auto battery = mAgents->battery;
        reply->set_hasbattery(battery->hasBattery());
        reply->set_ispresent(battery->present());
        reply->set_charger((BatteryState_BatteryCharger)battery->charger());
        reply->set_chargelevel(battery->chargeLevel());
        reply->set_health((BatteryState_BatteryHealth)battery->health());
        reply->set_status((BatteryState_BatteryStatus)battery->status());
        return Status::OK;
    }

    Status setGps(ServerContext* context,
                  const GpsState* request,
                  GpsState* reply) override {
        auto location = mAgents->location;
        struct timeval tVal;
        memset(&tVal, 0, sizeof(tVal));
        gettimeofday(&tVal, NULL);

        location->gpsSetPassiveUpdate(request->passiveupdate());
        location->gpsSendLoc(request->latitude(), request->longitude(),
                             request->elevation(), request->speed(),
                             request->heading(), request->satellites(), &tVal);
        return getGps(context, nullptr, reply);
    }

    Status getGps(ServerContext* context,
                  const ::google::protobuf::Empty* request,
                  GpsState* reply) override {
        auto location = mAgents->location;
        double lat, lon, speed, heading, elevation;
        int32_t count;

        // TODO(jansene):Implement in underlying agent.
        reply->set_passiveupdate(location->gpsGetPassiveUpdate());
        location->gpsGetLoc(&lat, &lon, &elevation, &speed, &heading, &count);

        reply->set_latitude(lat);
        reply->set_longitude(lon);
        reply->set_speed(speed);
        reply->set_heading(heading);
        reply->set_elevation(elevation);
        reply->set_satellites(count);
        return Status::OK;
    }

    Status sendFingerprint(ServerContext* context,
                           const FingerprintEvent* request,
                           ::google::protobuf::Empty* reply) override {
        mAgents->finger->setTouch(request->istouching(), request->touchid());
        return Status::OK;
    }

    Status sendKey(ServerContext* context,
                   const KeyboardEvent* request,
                   ::google::protobuf::Empty* reply) override {
        mKeyEventSender.send(request);
        return Status::OK;
    }

    Status sendMouse(ServerContext* context,
                     const MouseEvent* request,
                     ::google::protobuf::Empty* reply) override {
        mAgents->user_event->sendMouseEvent(request->x(), request->y(), 0,
                                            request->buttons(), 0);
        return Status::OK;
    }

    Status sendTouch(ServerContext* context,
                     const TouchEvent* request,
                     ::google::protobuf::Empty* reply) override {
        mTouchEventSender.send(request);
        return Status::OK;
    }

    Status sendRotary(ServerContext* context,
                      const RotaryEvent* request,
                      ::google::protobuf::Empty* reply) override {
        mAgents->user_event->sendRotaryEvent(request->delta());
        return Status::OK;
    }

    Status getVmConfiguration(ServerContext* context,
                              const ::google::protobuf::Empty* request,
                              VmConfiguration* reply) override {
        ::VmConfiguration config;
        mAgents->vm->getVmConfiguration(&config);
        reply->set_hypervisortype(
                (VmConfiguration_VmHypervisorType)(config.hypervisorType));
        reply->set_numberofcpucores(config.numberOfCpuCores);
        reply->set_ramsizebytes(config.ramSizeBytes);
        return Status::OK;
    }

    Status getScreenshot(ServerContext* context,
                         const ImageFormat* request,
                         Image* reply) override {
        auto start = System::get()->getUnixTimeUs();
        auto desiredFormat = android::emulation::ImageFormat::PNG;
        if (request->format() == ImageFormat_ImgFormat_RAW) {
            desiredFormat = android::emulation::ImageFormat::RAW;
        }

        // Screenshots can come from either the gl renderer, or the guest.
        const auto& renderer = android_getOpenglesRenderer();
        android::emulation::Image img = android::emulation::takeScreenshot(
                desiredFormat, SKIN_ROTATION_0, renderer.get(),
                mAgents->display->getFrameBuffer);

        reply->set_height(img.getHeight());
        reply->set_width(img.getWidth());
        reply->set_image(img.getPixelBuf(), img.getPixelCount());
        reply->mutable_format()->mutable_rotation()->set_rotation(
                ::android::emulation::control::
                        Rotation_SkinRotation_SKIN_ROTATION_0);
        switch (img.getImageFormat()) {
            case android::emulation::ImageFormat::PNG:
                reply->mutable_format()->set_format(ImageFormat_ImgFormat_PNG);
                break;
            case android::emulation::ImageFormat::RGB888:
                reply->mutable_format()->set_format(
                        ImageFormat_ImgFormat_RGB888);
                break;
            case android::emulation::ImageFormat::RGBA8888:
                reply->mutable_format()->set_format(
                        ImageFormat_ImgFormat_RGBA8888);
                break;
            default:
                LOG(ERROR) << "Unknown format retrieved during snapshot";
        }
        return Status::OK;
    }

    Status usePhone(ServerContext* context,
                    const TelephoneOperation* request,
                    TelephoneResponse* reply) override {
        // We assume that the int mappings are consistent..
        TelephonyOperation operation = (TelephonyOperation)request->operation();
        std::string phoneNr = request->number();
        TelephonyResponse response =
                mAgents->telephony->telephonyCmd(operation, phoneNr.c_str());
        reply->set_response(
                (::android::emulation::control::TelephoneResponse_Response)
                        response);
        return Status::OK;
    }

    Status requestRtcStream(ServerContext* context,
                            const ::google::protobuf::Empty* request,
                            RtcId* reply) override {
        std::string id = base::Uuid::generate().toString();
        mRtcBridge->connect(id);
        reply->set_guid(id);
        return Status::OK;
    }

    Status sendJsepMessage(ServerContext* context,
                           const JsepMsg* request,
                           ::google::protobuf::Empty* reply) override {
        std::string id = request->id().guid();
        std::string msg = request->message();
        mRtcBridge->acceptJsepMessage(id, msg);
        return Status::OK;
    }

    Status receiveJsepMessage(ServerContext* context,
                              const RtcId* request,
                              JsepMsg* reply) override {
        std::string msg;
        std::string id = request->guid();
        // Block and wait for at most 5 seconds.
        mRtcBridge->nextMessage(id, &msg, k5SecondsWait);
        reply->mutable_id()->set_guid(request->guid());
        reply->set_message(msg);
        return Status::OK;
    }

    Status getSnapshot(ServerContext* context,
                       const Snapshot* request,
                       ServerWriter<Snapshot>* writer) override {
        Snapshot result;
        result.set_success(true);

        for (auto snapshot :
             android::snapshot::Snapshot::getExistingSnapshots()) {
            std::string datadir = snapshot.dataDir();

            if (snapshot.name() == request->snapshot_id()) {
                CallbackStreambufWriter csb(
                        k128KB, [writer](char* bytes, std::size_t len) {
                            Snapshot msg;
                            msg.set_payload(std::string(bytes, len));
                            return writer->Write(msg);
                        });
                GzipOutputStream gzout(&csb);
                TarWriter tw(snapshot.dataDir(), gzout);
                result.set_success(tw.addDirectory(".") && tw.close());
            }
        }
        writer->Write(result);
        return Status::OK;
    }

    Status putSnapshot(ServerContext* context,
                       ::grpc::ServerReader<Snapshot>* reader,
                       Snapshot* reply) override {
        Snapshot msg;
        std::string id = Uuid::generate().toString();
        std::string tmpSnap = android::snapshot::getSnapshotDir(id.c_str());

        reply->set_success(true);
        auto cb = [reader, &msg, &id](char** new_eback, char** new_gptr,
                                      char** new_egptr) {
            // Drop messages without bytes.
            bool incoming = reader->Read(&msg);

            // First message likely only has snapshot id information and no
            // bytes.
            if (msg.snapshot_id().size() > 0) {
                std::cout << msg.snapshot_id() << std::endl;
                id = msg.snapshot_id();
            }
            while (msg.payload().size() == 0 && incoming) {
                incoming = reader->Read(&msg);
            }
            if (incoming) {
                *new_eback = (char*)msg.payload().data();
                *new_gptr = *new_eback;
                *new_egptr = *new_gptr + msg.payload().size();
            }
            return incoming;
        };

        CallbackStreambufReader csr(cb);
        GzipInputStream gzin(&csr);
        TarReader tr(tmpSnap, gzin);
        auto entry = tr.first();
        while (entry.valid) {
            if (!tr.extract(entry)) {
                reply->set_success(false);
                reply->set_err("Failed to extract: " + entry.name);
            }
            entry = tr.next(entry);
        }
        reply->set_snapshot_id(id);
        std::string finalDest = android::snapshot::getSnapshotDir(id.c_str());
        LOG(INFO) << "Moving " << tmpSnap << " --> " << finalDest;
        path_delete_dir(finalDest.c_str());
        std::rename(tmpSnap.c_str(), finalDest.c_str());
        return Status::OK;
    }

    Status listSnapshots(ServerContext* context,
                         const ::google::protobuf::Empty* request,
                         SnapshotList* reply) override {
        for (auto snapshot :
             android::snapshot::Snapshot::getExistingSnapshots()) {
            auto protobuf = snapshot.getGeneralInfo();

            if (protobuf && snapshot.checkValid(false)) {
                auto details = reply->add_snapshots();
                details->set_snapshot_id(snapshot.name());
                *details->mutable_details() = *protobuf;
            }
        }

        return Status::OK;
    }

private:
    const AndroidConsoleAgents* mAgents;
    keyboard::EmulatorKeyEventSender mKeyEventSender;
    TouchEventSender mTouchEventSender;
    RtcBridge* mRtcBridge;
    RingStreambuf
            mLogcatBuffer;  // A ring buffer that tracks the logcat output.

    static constexpr uint32_t k128KB = (128 * 1024) - 1;
    static constexpr uint16_t k5SecondsWait = 5 * 1000;
    const uint16_t kNoWait = 0;
};  // namespace control

using Builder = EmulatorControllerService::Builder;

Builder::Builder() : mCredentials{grpc::InsecureServerCredentials()} {}

Builder& Builder::withConsoleAgents(
        const AndroidConsoleAgents* const consoleAgents) {
    mAgents = consoleAgents;
    return *this;
}

Builder& Builder::withRtcBridge(RtcBridge* bridge) {
    mBridge = bridge;
    return *this;
}

Builder& Builder::withCertAndKey(std::string certfile,
                                 std::string privateKeyFile) {
    if (!System::get()->pathExists(certfile)) {
        LOG(WARNING) << "Cannot find certfile: " << certfile
                     << " security will be disabled.";
        return *this;
    }

    if (!!System::get()->pathExists(privateKeyFile)) {
        LOG(WARNING) << "Cannot find private key file: " << privateKeyFile
                     << " security will be disabled.";
        return *this;
    }

    std::ifstream key_file(privateKeyFile);
    std::string key((std::istreambuf_iterator<char>(key_file)),
                    std::istreambuf_iterator<char>());

    std::ifstream cert_file(certfile);
    std::string cert((std::istreambuf_iterator<char>(cert_file)),
                     std::istreambuf_iterator<char>());

    grpc::SslServerCredentialsOptions::PemKeyCertPair keycert = {key, cert};
    grpc::SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_key_cert_pairs.push_back(keycert);
    mCredentials = grpc::SslServerCredentials(ssl_opts);

    // We installed tls, so we are going public with this!
    mBindAddress = "0.0.0.0";
    return *this;
}

Builder& Builder::withPort(int port) {
    mPort = port;
    return *this;
}

std::unique_ptr<EmulatorControllerService> Builder::build() {
    if (mAgents == nullptr) {
        // Excuse me?
        return nullptr;
    }

    std::string server_address = mBindAddress + ":" + std::to_string(mPort);
    std::unique_ptr<EmulatorController::Service> controller(
            new EmulatorControllerImpl(mAgents, mBridge));
    std::unique_ptr<waterfall::Waterfall::Service> wfallforwarder(
            new WaterfallImpl());

    ServerBuilder builder;
    builder.AddListeningPort(server_address, mCredentials);
    builder.RegisterService(controller.release());
    builder.RegisterService(wfallforwarder.release());

    // Register logging & metrics interceptor.
    std::vector<std::unique_ptr<
            grpc::experimental::ServerInterceptorFactoryInterface>>
            creators;
    creators.emplace_back(std::make_unique<StdOutLoggingInterceptorFactory>());
    creators.emplace_back(std::make_unique<MetricsInterceptorFactory>());
    builder.experimental().SetInterceptorCreators(std::move(creators));

    // TODO(jansene): It seems that we can easily overload the server with
    // touch events. if the gRPC server runs out of threads to serve
    // requests it appears to terminate ungoing requests. If one of those
    // requests happens to have the event lock we will lock up the emulator.
    // This is a work around until we have a proper solution.
    builder.SetSyncServerOption(ServerBuilder::MAX_POLLERS, 1024);

    auto service = builder.BuildAndStart();
    if (!service)
        return nullptr;

    fprintf(stderr, "Started GRPC server at %s\n", server_address.c_str());
    return std::unique_ptr<EmulatorControllerService>(
            new EmulatorControllerServiceImpl(mPort, controller.release(),
                                              wfallforwarder.release(),
                                              service.release()));
}

}  // namespace control
}  // namespace emulation
}  // namespace android
