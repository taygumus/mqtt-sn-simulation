//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#ifndef MODULES_SERVER_MQTTSNSERVER_H_
#define MODULES_SERVER_MQTTSNSERVER_H_

#include "../MqttSNApp.h"
#include "types/shared/MsgType.h"
#include "types/shared/ReturnCode.h"
#include "types/shared/QoS.h"
#include "types/shared/TopicIdType.h"
#include "types/shared/ClientState.h"
#include "types/shared/TagInfo.h"
#include "types/server/GatewayState.h"
#include "types/server/ClientType.h"
#include "types/server/ClientInfo.h"
#include "types/server/DataInfo.h"
#include "types/server/PublisherInfo.h"
#include "types/server/TopicInfo.h"
#include "types/server/RetainMessageInfo.h"
#include "types/server/MessageInfo.h"
#include "types/server/RequestInfo.h"
#include "types/server/RegisterInfo.h"
#include "types/server/SubscriberTopicInfo.h"
#include "types/server/SubscriberInfo.h"

namespace mqttsn {

class MqttSNServer : public MqttSNApp
{
    protected:
        // parameters
        uint16_t advertiseInterval;
        double activeClientsCheckInterval;
        double asleepClientsCheckInterval;
        double pendingRetainCheckInterval;
        double requestsCheckInterval;
        double registrationsCheckInterval;
        double awakenSubscriberCheckInterval;
        double messagesClearInterval;

        // gateway state management
        inet::ClockEvent* stateChangeEvent = nullptr;
        GatewayState currentState;

        // online gateway state
        inet::ClockEvent* advertiseEvent = nullptr;

        static int gatewayIdCounter;
        uint8_t gatewayId = 0;

        std::map<std::pair<inet::L3Address, int>, ClientInfo> clients;
        inet::ClockEvent* activeClientsCheckEvent = nullptr;
        inet::ClockEvent* asleepClientsCheckEvent = nullptr;

        std::map<std::pair<inet::L3Address, int>, PublisherInfo> publishers;

        std::map<std::string, uint16_t> topicsToIds;
        std::map<uint16_t, TopicInfo> idsToTopics;
        std::set<uint16_t> topicIds;
        uint16_t currentTopicId = 0;

        std::map<uint16_t, RetainMessageInfo> retainMessages;
        std::set<uint16_t> retainMessageIds;

        inet::ClockEvent* pendingRetainCheckEvent = nullptr;
        std::map<std::pair<inet::L3Address, int>, MessageInfo> pendingRetainMessages;

        std::map<uint16_t, MessageInfo> messages;
        std::set<uint16_t> messageIds;
        uint16_t currentMessageId = 0;

        inet::ClockEvent* requestsCheckEvent = nullptr;
        std::map<uint16_t, RequestInfo> requests;
        std::set<uint16_t> requestIds;
        uint16_t currentRequestId = 0;

        inet::ClockEvent* registrationsCheckEvent = nullptr;
        std::map<uint16_t, RegisterInfo> registrations;
        std::set<uint16_t> registrationIds;
        uint16_t currentRegistrationId = 0;

        std::map<std::pair<inet::L3Address, int>, SubscriberInfo> subscribers;
        std::map<uint16_t, std::set<QoS>> topicIdToQoS;
        std::map<std::pair<uint16_t, QoS>, std::set<std::pair<inet::L3Address, int>>> subscriptions;

        // clear events
        inet::ClockEvent* messagesClearEvent = nullptr;

    protected:
        // initialization
        virtual void levelOneInit() override;

        // application base
        virtual void finish() override;

        // lifecycle
        virtual void handleStartOperation(inet::LifecycleOperation* operation) override;
        virtual void handleStopOperation(inet::LifecycleOperation* operation) override;
        virtual void handleCrashOperation(inet::LifecycleOperation* operation) override;

        // message handling
        virtual void handleMessageWhenUp(omnetpp::cMessage* msg) override;

        // gateway state management
        virtual void handleStateChangeEvent();
        virtual void updateCurrentState(GatewayState nextState);
        virtual void scheduleOnlineStateEvents();
        virtual void cancelOnlineStateEvents();
        virtual void cancelOnlineStateClockEvents();

        virtual bool fromOfflineToOnline();
        virtual bool fromOnlineToOffline();

        virtual bool performStateTransition(GatewayState currentState, GatewayState nextState);
        virtual double getStateInterval(GatewayState currentState);
        virtual std::string getGatewayStateAsString();

        // gateway methods
        virtual void setGatewayId();

        // incoming packet handling
        virtual void processPacket(inet::Packet* pk) override;

        virtual void processPacketByMessageType(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, MsgType msgType,
                                                ClientInfo* clientInfo);

        virtual bool isValidPacket(const inet::L3Address& srcAddress, const int& srcPort, MsgType msgType, ClientInfo*& clientInfo);

        // incoming packet type methods
        virtual void processSearchGw();
        virtual void processConnect(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processWillTopic(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, bool isDirectUpdate = false);
        virtual void processWillMsg(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, bool isDirectUpdate = false);
        virtual void processPingReq(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, ClientInfo* clientInfo);
        virtual void processPingResp(const inet::L3Address& srcAddress, const int& srcPort, ClientInfo* clientInfo);
        virtual void processDisconnect(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, ClientInfo* clientInfo);
        virtual void processRegister(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processPublish(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processPublishMinusOne(inet::Packet* pk);
        virtual void processPubRel(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processSubscribe(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processUnsubscribe(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processRegAck(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processPubAck(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processPubRec(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);
        virtual void processPubComp(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort);

        // outgoing packet handling
        virtual void sendAdvertise();

        virtual void sendBaseWithReturnCode(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, ReturnCode returnCode);

        virtual void sendMsgIdWithTopicIdPlus(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, uint16_t topicId,
                                              uint16_t msgId, ReturnCode returnCode);

        virtual void sendBaseWithMsgId(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, uint16_t msgId);

        virtual void sendSubAck(const inet::L3Address& destAddress, const int& destPort, QoS qosFlag, uint16_t topicId, uint16_t msgId,
                                ReturnCode returnCode);

        virtual void sendRegister(const inet::L3Address& destAddress, const int& destPort, uint16_t topicId, uint16_t msgId,
                                  const std::string& topicName);

        virtual void sendPublish(const inet::L3Address& destAddress, const int& destPort, bool dupFlag, QoS qosFlag, bool retainFlag,
                                 TopicIdType topicIdTypeFlag, uint16_t topicId, uint16_t msgId, const std::string& data, const TagInfo& tagInfo);

        // event handlers
        virtual void handleAdvertiseEvent();

        virtual void handleActiveClientsCheckEvent();
        virtual void handleAsleepClientsCheckEvent();
        virtual void handlePendingRetainCheckEvent();
        virtual void handleRequestsCheckEvent();
        virtual void handleRegistrationsCheckEvent();
        virtual void handleAwakenSubscriberCheckEvent(omnetpp::cMessage* msg);

        virtual void handleMessagesClearEvent();

        // client methods
        virtual void cleanClientSession(const inet::L3Address& clientAddress, const int& clientPort, ClientType clientType);
        virtual void updateClientType(ClientInfo* clientInfo, ClientType clientType);
        virtual ClientInfo* addNewClient(const inet::L3Address& clientAddress, const int& clientPort);
        virtual ClientInfo* getClientInfo(const inet::L3Address& clientAddress, const int& clientPort);

        // publisher methods
        virtual PublisherInfo* getPublisherInfo(const inet::L3Address& publisherAddress, const int& publisherPort, bool insertIfNotFound = false);

        // topic methods
        virtual void fillWithPredefinedTopics();
        virtual void addNewTopic(const std::string& topicName, uint16_t topicId, TopicIdType topicIdType);
        virtual void checkTopicsToIds(const std::string& topicName, uint16_t topicId);
        virtual uint16_t getTopicByName(const std::string& topicName);
        virtual TopicInfo getTopicById(uint16_t topicId);
        virtual TopicIdType getTopicIdType(uint16_t topicLength);

        // retain message methods
        virtual void addNewRetainMessage(uint16_t topicId, bool dup, QoS qos, TopicIdType topicIdType, const std::string& data);
        virtual void addNewPendingRetainMessage(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId, QoS qos);

        // message methods
        virtual void addNewMessage(const MessageInfo& messageInfo);
        virtual void addAndMarkMessage(const MessageInfo& messageInfo, bool& isMessageAdded);
        virtual void deleteMessage(std::map<uint16_t, MessageInfo>::iterator& messageIt, std::set<uint16_t>::iterator& messageIdIt);
        virtual void deleteAllocatedMessages(const std::vector<MessageInfo*>& messages);

        // request message methods
        virtual MessageInfo* getRequestMessageInfo(const RequestInfo& requestInfo, std::vector<MessageInfo*>& allocatedObjects);

        // request handling methods
        virtual void dispatchPublishToSubscribers(const MessageInfo& messageInfo);

        virtual void processRequestForActiveSubscriber(const inet::L3Address& subscriberAddress, int subscriberPort,
                                                       const MessageInfo& messageInfo, QoS resultQoS, bool& isMessageAdded);

        virtual void processRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, const MessageInfo& messageInfo,
                                    QoS resultQoS, bool& isMessageAdded);

        virtual void bufferRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, const MessageInfo& messageInfo,
                                   bool& isMessageAdded);

        virtual void addAndSendPublishRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, const MessageInfo& messageInfo,
                                              QoS resultQoS, uint16_t messagesKey = 0, uint16_t retainMessagesKey = 0);

        virtual void addNewRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, MsgType messageType, bool sendAtLeastOnce,
                                   uint16_t messagesKey = 0, uint16_t retainMessagesKey = 0);

        virtual void deleteRequest(std::map<uint16_t, RequestInfo>::iterator& requestIt, std::set<uint16_t>::iterator& requestIdIt);

        virtual bool isValidRequest(uint16_t requestId, MsgType messageType, std::map<uint16_t, RequestInfo>::iterator& requestIt,
                                    std::set<uint16_t>::iterator& requestIdIt);

        virtual bool processRequestAck(uint16_t requestId, MsgType messageType);

        // registration methods
        virtual void manageRegistration(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId);

        virtual void addNewRegistration(const inet::L3Address& subscriberAddress, const int& subscriberPort, const std::string& topicName,
                                        uint16_t topicId);

        virtual void deleteRegistration(std::map<uint16_t, RegisterInfo>::iterator& registrationIt, std::set<uint16_t>::iterator& registrationIdIt);
        virtual bool processRegistrationAck(uint16_t registrationId);

        // subscriber methods
        virtual void setAllSubscriberTopics(const inet::L3Address& subscriberAddress, const int& subscriberPort, bool isRegistered,
                                            bool skipPredefinedTopics = true);

        virtual void handleSubscriberPingRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort);

        virtual void manageAwakenSubscriberEvent(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                                 SubscriberInfo* subscriberInfo);

        virtual bool isTopicRegisteredForSubscriber(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId);

        virtual SubscriberInfo* getSubscriberInfo(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                                  bool insertIfNotFound = false);

        virtual SubscriberTopicInfo* getSubscriberTopicInfo(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId);
        virtual ClientInfo* getSubscriberClientInfo(const inet::L3Address& subscriberAddress, const int& subscriberPort);

        // subscription methods
        virtual void deleteSubscriptionIfExists(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId);

        virtual bool findSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId,
                                      std::pair<uint16_t, QoS>& subscriptionKey);

        virtual bool insertSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId,
                                        TopicIdType topicIdType, QoS qos);

        virtual bool deleteSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                        const std::pair<uint16_t, QoS>& subscriptionKey);

        virtual std::set<std::pair<uint16_t, QoS>> getSubscriptionKeysByTopicId(uint16_t topicId);

        // congestion methods
        virtual bool checkClientsCongestion();
        virtual bool checkIDSpaceCongestion(const std::set<uint16_t>& usedIds, bool allowMaxValue = true);
        virtual bool checkPublishCongestion(QoS qos, bool retain);

        // clear methods
        virtual void clearPublishersData();
        virtual void clearSubscribersData();

    public:
        MqttSNServer() {};
        ~MqttSNServer();
};

} /* namespace mqttsn */

#endif /* MODULES_SERVER_MQTTSNSERVER_H_ */
