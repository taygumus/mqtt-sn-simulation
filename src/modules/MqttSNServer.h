#ifndef MODULES_MQTTSNSERVER_H_
#define MODULES_MQTTSNSERVER_H_

#include "MqttSNApp.h"

namespace mqttsn {

class MqttSNServer : public MqttSNApp
{
    protected:
        // parameters
        inet::clocktime_t startAdvertise;
        inet::clocktime_t stopAdvertise;

        // state
        inet::ClockEvent *advertiseEvent = nullptr;

        // statistics
        int numAdvertiseSent = 0;

    protected:
        virtual void initialize(int stage) override;
        virtual void handleMessageWhenUp(omnetpp::cMessage *msg) override;
        virtual void finish() override;
        virtual void refreshDisplay() const override;

        virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
        virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
        virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

        virtual void processPacket(inet::Packet *msg) override;
        virtual void sendPacket();

    public:
        MqttSNServer() {};
        ~MqttSNServer();
};

} /* namespace mqttsn */

#endif /* MODULES_MQTTSNSERVER_H_ */
