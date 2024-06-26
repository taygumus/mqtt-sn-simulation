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

#include "MqttSNServer.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/common/TimeTag_m.h"
#include "helpers/StringHelper.h"
#include "helpers/PacketHelper.h"
#include "helpers/NumericHelper.h"
#include "types/shared/Length.h"
#include "tags/IdentifierTag.h"
#include "messages/MqttSNAdvertise.h"
#include "messages/MqttSNConnect.h"
#include "messages/MqttSNBase.h"
#include "messages/MqttSNBaseWithReturnCode.h"
#include "messages/MqttSNBaseWithWillTopic.h"
#include "messages/MqttSNBaseWithWillMsg.h"
#include "messages/MqttSNDisconnect.h"
#include "messages/MqttSNPingReq.h"
#include "messages/MqttSNRegister.h"
#include "messages/MqttSNMsgIdWithTopicIdPlus.h"
#include "messages/MqttSNPublish.h"
#include "messages/MqttSNSubscribe.h"
#include "messages/MqttSNSubAck.h"
#include "messages/MqttSNUnsubscribe.h"

namespace mqttsn {

Define_Module(MqttSNServer);

int MqttSNServer::gatewayIdCounter = -1;

void MqttSNServer::levelOneInit()
{
    stateChangeEvent = new inet::ClockEvent("stateChangeTimer");
    currentState = GatewayState::OFFLINE;

    advertiseInterval = par("advertiseInterval");
    advertiseEvent = new inet::ClockEvent("advertiseTimer");

    gatewayIdCounter = -1;

    activeClientsCheckInterval = par("activeClientsCheckInterval");
    activeClientsCheckEvent = new inet::ClockEvent("activeClientsCheckTimer");

    asleepClientsCheckInterval = par("asleepClientsCheckInterval");
    asleepClientsCheckEvent = new inet::ClockEvent("asleepClientsCheckTimer");

    fillWithPredefinedTopics();

    pendingRetainCheckInterval = par("pendingRetainCheckInterval");
    pendingRetainCheckEvent = new inet::ClockEvent("pendingRetainCheckTimer");

    requestsCheckInterval = par("requestsCheckInterval");
    requestsCheckEvent = new inet::ClockEvent("requestsCheckTimer");

    registrationsCheckInterval = par("registrationsCheckInterval");
    registrationsCheckEvent = new inet::ClockEvent("registrationsCheckTimer");

    awakenSubscriberCheckInterval = par("awakenSubscriberCheckInterval");

    messagesClearInterval = par("messagesClearInterval");
    messagesClearEvent = new inet::ClockEvent("messagesClearTimer");
}

void MqttSNServer::finish()
{
    clearPublishersData();
    clearSubscribersData();

    MqttSNApp::finish();
}

void MqttSNServer::handleStartOperation(inet::LifecycleOperation* operation)
{
    MqttSNApp::socketConfiguration();

    setGatewayId();

    EV << "Current gateway state: " << getGatewayStateAsString() << std::endl;

    double currentStateInterval = getStateInterval(currentState);
    if (currentStateInterval != -1) {
        scheduleClockEventAt(currentStateInterval, stateChangeEvent);
    }
}

void MqttSNServer::handleStopOperation(inet::LifecycleOperation* operation)
{
    cancelEvent(stateChangeEvent);
    cancelOnlineStateEvents();

    MqttSNApp::socket.close();
}

void MqttSNServer::handleCrashOperation(inet::LifecycleOperation* operation)
{
    cancelOnlineStateClockEvents();

    MqttSNApp::socket.destroy();
}

void MqttSNServer::handleMessageWhenUp(omnetpp::cMessage* msg)
{
    if (msg == stateChangeEvent) {
        handleStateChangeEvent();
    }
    else if (msg == advertiseEvent) {
        handleAdvertiseEvent();
    }
    else if (msg == activeClientsCheckEvent) {
        handleActiveClientsCheckEvent();
    }
    else if (msg == asleepClientsCheckEvent) {
        handleAsleepClientsCheckEvent();
    }
    else if (msg == pendingRetainCheckEvent) {
        handlePendingRetainCheckEvent();
    }
    else if (msg == requestsCheckEvent) {
        handleRequestsCheckEvent();
    }
    else if (msg == registrationsCheckEvent) {
        handleRegistrationsCheckEvent();
    }
    else if (msg->hasPar("isAwakenSubscriberCheckEvent")) {
        handleAwakenSubscriberCheckEvent(msg);
    }
    else if (msg == messagesClearEvent) {
        handleMessagesClearEvent();
    }
    else {
        MqttSNApp::socket.processMessage(msg);
    }
}

void MqttSNServer::handleStateChangeEvent()
{
    // get the possible next state based on the current state
    GatewayState nextState = (currentState == GatewayState::ONLINE) ? GatewayState::OFFLINE : GatewayState::ONLINE;

    // get the interval for the next state
    double nextStateInterval = getStateInterval(nextState);
    if (nextStateInterval != -1) {
        scheduleClockEventAfter(nextStateInterval, stateChangeEvent);
    }

    // perform state transition and update if successful
    if (performStateTransition(currentState, nextState)) {
        updateCurrentState(nextState);
    }
}

void MqttSNServer::updateCurrentState(GatewayState nextState)
{
    currentState = nextState;
    EV << "Current gateway state: " << getGatewayStateAsString() << std::endl;
}

void MqttSNServer::scheduleOnlineStateEvents()
{
    scheduleClockEventAfter(advertiseInterval, advertiseEvent);
    scheduleClockEventAfter(activeClientsCheckInterval, activeClientsCheckEvent);
    scheduleClockEventAfter(asleepClientsCheckInterval, asleepClientsCheckEvent);
    scheduleClockEventAfter(pendingRetainCheckInterval, pendingRetainCheckEvent);
    scheduleClockEventAfter(requestsCheckInterval, requestsCheckEvent);
    scheduleClockEventAfter(registrationsCheckInterval, registrationsCheckEvent);
    scheduleClockEventAfter(messagesClearInterval, messagesClearEvent);
}

void MqttSNServer::cancelOnlineStateEvents()
{
    cancelEvent(advertiseEvent);
    cancelEvent(activeClientsCheckEvent);
    cancelEvent(asleepClientsCheckEvent);
    cancelEvent(pendingRetainCheckEvent);
    cancelEvent(requestsCheckEvent);
    cancelEvent(registrationsCheckEvent);
    cancelEvent(messagesClearEvent);
}

void MqttSNServer::cancelOnlineStateClockEvents()
{
    cancelClockEvent(stateChangeEvent);
    cancelClockEvent(advertiseEvent);
    cancelClockEvent(activeClientsCheckEvent);
    cancelClockEvent(asleepClientsCheckEvent);
    cancelClockEvent(pendingRetainCheckEvent);
    cancelClockEvent(requestsCheckEvent);
    cancelClockEvent(registrationsCheckEvent);
    cancelClockEvent(messagesClearEvent);
}

bool MqttSNServer::fromOfflineToOnline()
{
    EV << "Offline -> Online" << std::endl;
    scheduleOnlineStateEvents();

    return true;
}

bool MqttSNServer::fromOnlineToOffline()
{
    EV << "Online -> Offline" << std::endl;
    cancelOnlineStateEvents();

    return true;
}

bool MqttSNServer::performStateTransition(GatewayState currentState, GatewayState nextState)
{
    // determine the transition function based on current and next states
    switch (currentState) {
        case GatewayState::OFFLINE:
            if (nextState == GatewayState::ONLINE) {
                return fromOfflineToOnline();
            }
            break;

        case GatewayState::ONLINE:
            if (nextState == GatewayState::OFFLINE) {
                return fromOnlineToOffline();
            }
            break;

        default:
            break;
    }

    return false;
}


double MqttSNServer::getStateInterval(GatewayState currentState)
{
    // returns the interval duration for the given state
    switch (currentState) {
        case GatewayState::OFFLINE:
            return par("offlineStateInterval");

        case GatewayState::ONLINE:
            return par("onlineStateInterval");
    }
}

std::string MqttSNServer::getGatewayStateAsString()
{
    // get current gateway state as a string
    switch (currentState) {
        case GatewayState::OFFLINE:
            return "Offline";

        case GatewayState::ONLINE:
            return "Online";
    }
}

void MqttSNServer::setGatewayId()
{
    if (gatewayIdCounter < UINT8_MAX) {
        gatewayIdCounter++;
        gatewayId = gatewayIdCounter;
        return;
    }

    throw omnetpp::cRuntimeError("The gateway ID counter has reached its maximum limit");
}

void MqttSNServer::processPacket(inet::Packet* pk)
{
    inet::L3Address srcAddress = pk->getTag<inet::L3AddressInd>()->getSrcAddress();

    // discard packet if the server is OFFLINE or if the address is a self-broadcast
    if (currentState == GatewayState::OFFLINE || MqttSNApp::isSelfBroadcastAddress(srcAddress)) {
        delete pk;
        return;
    }

    EV << "Server received packet: " << inet::UdpSocket::getReceivedPacketInfo(pk) << std::endl;

    const auto& header = pk->peekData<MqttSNBase>();
    MqttSNApp::checkPacketIntegrity((inet::B) pk->getByteLength(), (inet::B) header->getLength());

    MsgType msgType = header->getMsgType();

    // if message type is PUBLISH and QoS is -1, process the packet and exit
    if (msgType == MsgType::PUBLISH && pk->peekData<MqttSNPublish>()->getQoSFlag() == QoS::QOS_MINUS_ONE) {
        processPublishMinusOne(pk);
        delete pk;
        return;
    }

    int srcPort = pk->getTag<inet::L4PortInd>()->getSrcPort();
    ClientInfo* clientInfo = nullptr;

    // validate received packet and get client information
    if (!isValidPacket(srcAddress, srcPort, msgType, clientInfo)) {
        delete pk;
        return;
    }

    // process packet based on the message type
    processPacketByMessageType(pk, srcAddress, srcPort, msgType, clientInfo);

    // delete packet after processing
    delete pk;
}

void MqttSNServer::processPacketByMessageType(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, MsgType msgType,
                                              ClientInfo* clientInfo)
{
    switch(msgType) {
        case MsgType::SEARCHGW:
            processSearchGw();
            break;

        case MsgType::CONNECT:
            processConnect(pk, srcAddress, srcPort);
            break;

        case MsgType::WILLTOPIC:
            processWillTopic(pk, srcAddress, srcPort);
            break;

        case MsgType::WILLTOPICUPD:
            processWillTopic(pk, srcAddress, srcPort, true);
            break;

        case MsgType::WILLMSG:
            processWillMsg(pk, srcAddress, srcPort);
            break;

        case MsgType::WILLMSGUPD:
            processWillMsg(pk, srcAddress, srcPort, true);
            break;

        case MsgType::PINGREQ:
            processPingReq(pk, srcAddress, srcPort, clientInfo);
            break;

        case MsgType::PINGRESP:
            processPingResp(srcAddress, srcPort, clientInfo);
            break;

        case MsgType::DISCONNECT:
            processDisconnect(pk, srcAddress, srcPort, clientInfo);
            break;

        case MsgType::REGISTER:
            updateClientType(clientInfo, ClientType::PUBLISHER);
            processRegister(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBLISH:
            updateClientType(clientInfo, ClientType::PUBLISHER);
            processPublish(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBREL:
            processPubRel(pk, srcAddress, srcPort);
            break;

        case MsgType::SUBSCRIBE:
            updateClientType(clientInfo, ClientType::SUBSCRIBER);
            processSubscribe(pk, srcAddress, srcPort);
            break;

        case MsgType::UNSUBSCRIBE:
            updateClientType(clientInfo, ClientType::SUBSCRIBER);
            processUnsubscribe(pk, srcAddress, srcPort);
            break;

        case MsgType::REGACK:
            processRegAck(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBACK:
            processPubAck(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBREC:
            processPubRec(pk, srcAddress, srcPort);
            break;

        case MsgType::PUBCOMP:
            processPubComp(pk, srcAddress, srcPort);
            break;

        default:
            break;
    }
}

bool MqttSNServer::isValidPacket(const inet::L3Address& srcAddress, const int& srcPort, MsgType msgType, ClientInfo*& clientInfo)
{
    switch(msgType) {
        // packet types that require an ACTIVE client state
        case MsgType::WILLTOPIC:
        case MsgType::WILLTOPICUPD:
        case MsgType::WILLMSG:
        case MsgType::WILLMSGUPD:
        case MsgType::PINGRESP:
        case MsgType::REGISTER:
        case MsgType::PUBLISH:
        case MsgType::PUBREL:
        case MsgType::SUBSCRIBE:
        case MsgType::UNSUBSCRIBE:
        case MsgType::REGACK:
            clientInfo = getClientInfo(srcAddress, srcPort);

            // discard packet if client is not found or not in ACTIVE state
            if (clientInfo == nullptr || (clientInfo->currentState != ClientState::ACTIVE)) {
                return false;
            }

            clientInfo->lastReceivedMsgTime = getClockTime();
            break;

        // packet types that require an ACTIVE or AWAKE client state
        case MsgType::PUBACK:
        case MsgType::PUBREC:
        case MsgType::PUBCOMP:
            clientInfo = getClientInfo(srcAddress, srcPort);

            // discard packet if client is not found or not in ACTIVE or AWAKE state
            if (clientInfo == nullptr ||
                (clientInfo->currentState != ClientState::ACTIVE && clientInfo->currentState != ClientState::AWAKE)) {

                return false;
            }

            clientInfo->lastReceivedMsgTime = getClockTime();
            break;

        // packet types that require an ACTIVE or ASLEEP client state
        case MsgType::PINGREQ:
        case MsgType::DISCONNECT:
            clientInfo = getClientInfo(srcAddress, srcPort);

            // discard packet if client is not found or not in ACTIVE or ASLEEP state
            if (clientInfo == nullptr ||
                (clientInfo->currentState != ClientState::ACTIVE && clientInfo->currentState != ClientState::ASLEEP)) {

                return false;
            }

            clientInfo->lastReceivedMsgTime = getClockTime();
            break;

        default:
            break;
    }

    return true;
}

void MqttSNServer::processSearchGw()
{
    MqttSNApp::sendGwInfo(gatewayId);
}

void MqttSNServer::processConnect(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNConnect>();

    // prevent client connection when its protocol ID is not supported
    if (payload->getProtocolId() != 0x01) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::REJECTED_NOT_SUPPORTED);
        return;
    }

    ClientInfo* clientInfo = getClientInfo(srcAddress, srcPort);
    std::string clientId = payload->getClientId();

    if (clientInfo != nullptr) {
        // if client is found, verify the client ID
        if (clientId != clientInfo->clientId) {
            sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::REJECTED_NOT_SUPPORTED);
            return;
        }

        ClientType clientType = clientInfo->clientType;

        // check the clean session flag
        if (payload->getCleanSessionFlag()) {
            // if clean session flag is set and client type is unspecified, reject the connection
            if (clientType == ClientType::CLIENT) {
                sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::REJECTED_NOT_SUPPORTED);
                return;
            }

            // perform clean session operation for the identified client type
            cleanClientSession(srcAddress, srcPort, clientType);
        }
        else {
            // check if the client is a subscriber
            if (clientType == ClientType::SUBSCRIBER) {
                // reset flags; all subscribed topics require re-registration
                setAllSubscriberTopics(srcAddress, srcPort, false);
            }
        }
    }
    else {
        // if client is not found, check for congestion before adding
        if (checkClientsCongestion()) {
            sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::REJECTED_CONGESTION);
            return;
        }

        // add the new client
        clientInfo = addNewClient(srcAddress, srcPort);
        clientInfo->clientId = clientId;
    }

    // update client information
    clientInfo->keepAliveDuration = payload->getDuration();
    clientInfo->currentState = ClientState::ACTIVE;
    clientInfo->lastReceivedMsgTime = getClockTime();

    bool will = payload->getWillFlag();

    if (will) {
        // update publisher information
        PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);
        publisherInfo->will = will;

        MqttSNApp::sendBase(srcAddress, srcPort, MsgType::WILLTOPICREQ);
        return;
    }

    sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::ACCEPTED);
}

void MqttSNServer::processWillTopic(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, bool isDirectUpdate)
{
    const auto& payload = pk->peekData<MqttSNBaseWithWillTopic>();

    // update publisher information
    PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);
    publisherInfo->willQoS = (QoS) payload->getQoSFlag();
    publisherInfo->willRetain = payload->getRetainFlag();
    publisherInfo->willTopic = payload->getWillTopic();

    if (isDirectUpdate) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::WILLTOPICRESP, ReturnCode::ACCEPTED);
        return;
    }

    MqttSNApp::sendBase(srcAddress, srcPort, MsgType::WILLMSGREQ);
}

void MqttSNServer::processWillMsg(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, bool isDirectUpdate)
{
    const auto& payload = pk->peekData<MqttSNBaseWithWillMsg>();

    // update publisher information
    PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);
    publisherInfo->willMsg = payload->getWillMsg();

    if (isDirectUpdate) {
        sendBaseWithReturnCode(srcAddress, srcPort, MsgType::WILLMSGRESP, ReturnCode::ACCEPTED);
        return;
    }

    sendBaseWithReturnCode(srcAddress, srcPort, MsgType::CONNACK, ReturnCode::ACCEPTED);
}

void MqttSNServer::processPingReq(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, ClientInfo* clientInfo)
{
    const auto& payload = pk->peekData<MqttSNPingReq>();
    std::string clientId = payload->getClientId();

    if (!clientId.empty()) {
        // check if the client ID matches the expected client ID
        if (clientInfo->clientId != clientId) {
            return;
        }

        if (clientInfo->clientType == ClientType::SUBSCRIBER) {
            // handle subscriber-related tasks at PINGREQ
            handleSubscriberPingRequest(srcAddress, srcPort);

            // update subscriber state
            clientInfo->currentState = ClientState::AWAKE;
            return;
        }
    }

    MqttSNApp::sendBase(srcAddress, srcPort, MsgType::PINGRESP);
}

void MqttSNServer::processPingResp(const inet::L3Address& srcAddress, const int& srcPort, ClientInfo* clientInfo)
{
    // update client information
    clientInfo->sentPingReq = false;

    EV << "Received ping response from client: " << srcAddress << ":" << srcPort << std::endl;
}

void MqttSNServer::processDisconnect(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort, ClientInfo* clientInfo)
{
    const auto& payload = pk->peekData<MqttSNDisconnect>();
    uint16_t sleepDuration = payload->getDuration();

    // update client information
    clientInfo->sleepDuration = sleepDuration;
    clientInfo->currentState = (sleepDuration > 0) ? ClientState::ASLEEP : ClientState::DISCONNECTED;

    // ACK with DISCONNECT message
    MqttSNApp::sendDisconnect(srcAddress, srcPort, sleepDuration);
}

void MqttSNServer::processRegister(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNRegister>();
    uint16_t topicId = payload->getTopicId();
    uint16_t msgId = payload->getMsgId();

    // extract and sanitize the topic name from the payload
    std::string topicName = StringHelper::sanitizeSpaces(payload->getTopicName());
    uint16_t topicLength = topicName.length();

    // reject registration if the topic name length is less than the minimum required
    if (!MqttSNApp::isMinTopicLength(topicLength)) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, topicId, msgId, ReturnCode::REJECTED_NOT_SUPPORTED);
        return;
    }

    // encode the sanitized topic name to Base64 for consistent key handling
    std::string encodedTopicName = StringHelper::base64Encode(topicName);

    // check if the topic is already registered; if yes, send ACCEPTED response, otherwise register the topic
    auto it = topicsToIds.find(encodedTopicName);
    if (it != topicsToIds.end()) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, it->second, msgId, ReturnCode::ACCEPTED);
        return;
    }

    // check if the maximum number of topics is reached; if not, set a new available topic ID
    if (!MqttSNApp::setNextAvailableId(topicIds, currentTopicId, false)) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, topicId, msgId, ReturnCode::REJECTED_CONGESTION);
        return;
    }

    addNewTopic(encodedTopicName, currentTopicId, getTopicIdType(topicLength));

    // send REGACK response with the new topic ID and ACCEPTED status
    sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::REGACK, currentTopicId, msgId, ReturnCode::ACCEPTED);
}

void MqttSNServer::processPublish(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNPublish>();
    uint16_t topicId = payload->getTopicId();
    uint16_t msgId = payload->getMsgId();

    // check if the topic is registered; if not, send a return code
    auto it = idsToTopics.find(topicId);
    if (it == idsToTopics.end()) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, topicId, msgId, ReturnCode::REJECTED_INVALID_TOPIC_ID);
        return;
    }

    TopicIdType topicIdType = (TopicIdType) payload->getTopicIdTypeFlag();

    // check for inconsistency in the received topic ID type
    if (it->second.topicIdType != topicIdType) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, topicId, msgId, ReturnCode::REJECTED_NOT_SUPPORTED);
        return;
    }

    QoS qos = (QoS) payload->getQoSFlag();
    bool retain = payload->getRetainFlag();

    // check for congestion; if congested, send a rejection code
    if (checkPublishCongestion(qos, retain)) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, topicId, msgId, ReturnCode::REJECTED_CONGESTION);
        return;
    }

    bool dup = payload->getDupFlag();
    std::string data = payload->getData();

    if (retain) {
        // add a new retained message for the specified topic
        addNewRetainMessage(topicId, dup, qos, topicIdType, data);
    }

    TagInfo tagInfo;
    tagInfo.timestamp = inet::ClockTime::SIMTIME_AS_CLOCKTIME(payload->findTag<inet::CreationTimeTag>()->getCreationTime());
    tagInfo.identifier = payload->findTag<IdentifierTag>()->getIdentifier();

    MessageInfo messageInfo;
    messageInfo.topicId = topicId;
    messageInfo.topicIdType = topicIdType;
    messageInfo.dup = dup;
    messageInfo.qos = qos;
    messageInfo.retain = retain;
    messageInfo.data = data;
    messageInfo.tagInfo = tagInfo;

    if (qos == QoS::QOS_ZERO) {
        // handling QoS 0
        dispatchPublishToSubscribers(messageInfo);
        return;
    }

    if (qos == QoS::QOS_ONE) {
        // handling QoS 1
        dispatchPublishToSubscribers(messageInfo);
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, topicId, msgId, ReturnCode::ACCEPTED);
        return;
    }

    // handling QoS 2; message ID check needed
    if (msgId == 0) {
        sendMsgIdWithTopicIdPlus(srcAddress, srcPort, MsgType::PUBACK, topicId, msgId, ReturnCode::REJECTED_NOT_SUPPORTED);
        return;
    }

    // update publisher information and send a PUBREC response
    PublisherInfo* publisherInfo = getPublisherInfo(srcAddress, srcPort, true);

    DataInfo dataInfo;
    dataInfo.topicId = topicId;
    dataInfo.topicIdType = topicIdType;
    dataInfo.retain = retain;
    dataInfo.data = data;
    dataInfo.tagInfo = tagInfo;

    // save message data for reuse
    publisherInfo->messages[msgId] = dataInfo;

    // send PUBlish RECeived
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBREC, msgId);
}

void MqttSNServer::processPublishMinusOne(inet::Packet* pk)
{
    const auto& payload = pk->peekData<MqttSNPublish>();
    uint16_t topicId = payload->getTopicId();
    TopicIdType topicIdType = (TopicIdType) payload->getTopicIdTypeFlag();

    // handle the case where the topic is not registered or inconsistencies are detected
    auto it = idsToTopics.find(topicId);
    if (it == idsToTopics.end() || it->second.topicIdType != topicIdType || topicIdType != TopicIdType::PRE_DEFINED_TOPIC_ID) {
        return;
    }

    TagInfo tagInfo;
    tagInfo.timestamp = inet::ClockTime::SIMTIME_AS_CLOCKTIME(payload->findTag<inet::CreationTimeTag>()->getCreationTime());;
    tagInfo.identifier = payload->findTag<IdentifierTag>()->getIdentifier();

    MessageInfo messageInfo;
    messageInfo.topicId = topicId;
    messageInfo.topicIdType = TopicIdType::PRE_DEFINED_TOPIC_ID;
    messageInfo.dup = false;
    messageInfo.qos = QoS::QOS_MINUS_ONE;
    messageInfo.retain = false;
    messageInfo.data = payload->getData();
    messageInfo.tagInfo = tagInfo;

    // handling QoS -1
    dispatchPublishToSubscribers(messageInfo);
}

void MqttSNServer::processPubRel(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    // check if the publisher exists for the given key
    auto publisherIt = publishers.find(std::make_pair(srcAddress, srcPort));
    if (publisherIt == publishers.end()) {
        return;
    }

    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();
    uint16_t msgId = payload->getMsgId();

    // access the messages associated with the publisher
    std::map<uint16_t, DataInfo>& messages = publisherIt->second.messages;

    // check if the message exists for the given message ID
    auto messageIt = messages.find(msgId);
    if (messageIt != messages.end()) {
        // process the original PUBLISH message only once; as required for QoS 2
        const DataInfo& dataInfo = messageIt->second;

        MessageInfo messageInfo;
        messageInfo.topicId = dataInfo.topicId;
        messageInfo.topicIdType = dataInfo.topicIdType;
        messageInfo.dup = false;
        messageInfo.qos = QoS::QOS_TWO;
        messageInfo.retain = dataInfo.retain;
        messageInfo.data = dataInfo.data;
        messageInfo.tagInfo = dataInfo.tagInfo;

        // handling QoS 2
        dispatchPublishToSubscribers(messageInfo);

        // after processing, delete the message from the map
        messages.erase(messageIt);
    }

    // send PUBlish COMPlete
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBCOMP, msgId);
}

void MqttSNServer::processSubscribe(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNSubscribe>();
    TopicIdType topicIdType = (TopicIdType) payload->getTopicIdTypeFlag();
    uint16_t topicId = payload->getTopicId();

    QoS qos = (QoS) payload->getQoSFlag();
    uint16_t msgId = payload->getMsgId();

    if (topicIdType == TopicIdType::PRE_DEFINED_TOPIC_ID) {
        // check for the predefined topic ID
        auto it = idsToTopics.find(topicId);
        if (it == idsToTopics.end() || it->second.topicIdType != topicIdType) {
            sendSubAck(srcAddress, srcPort, qos, 0, msgId, ReturnCode::REJECTED_INVALID_TOPIC_ID);
            return;
        }
    }
    else {
        // extract and sanitize the topic name from the payload
        std::string topicName = StringHelper::sanitizeSpaces(payload->getTopicName());
        uint16_t topicLength = topicName.length();

        // reject registration if the topic name length is less than the minimum required
        if (!MqttSNApp::isMinTopicLength(topicLength)) {
            sendSubAck(srcAddress, srcPort, qos, 0, msgId, ReturnCode::REJECTED_NOT_SUPPORTED);
            return;
        }

        // encode the sanitized topic name to Base64 for consistent key handling
        std::string encodedTopicName = StringHelper::base64Encode(topicName);

        // check if the topic is already registered; if not, register it
        auto it = topicsToIds.find(encodedTopicName);
        if (it == topicsToIds.end()) {
            // check if the maximum number of topics is reached; if not, set a new available topic ID
            if (!MqttSNApp::setNextAvailableId(topicIds, currentTopicId, false)) {
                sendSubAck(srcAddress, srcPort, qos, 0, msgId, ReturnCode::REJECTED_CONGESTION);
                return;
            }

            addNewTopic(encodedTopicName, currentTopicId, getTopicIdType(topicLength));
            topicId = currentTopicId;
        }
        else {
            topicId = it->second;
        }
    }

    // remove subscription if exists
    deleteSubscriptionIfExists(srcAddress, srcPort, topicId);

    // create a new subscription
    insertSubscription(srcAddress, srcPort, topicId, topicIdType, qos);

    // check for existing retain message and add in the queue if found
    addNewPendingRetainMessage(srcAddress, srcPort, topicId, qos);

    // send ACK message with ACCEPTED code
    sendSubAck(srcAddress, srcPort, qos, topicId, msgId, ReturnCode::ACCEPTED);
}

void MqttSNServer::processUnsubscribe(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNUnsubscribe>();
    TopicIdType topicIdType = (TopicIdType) payload->getTopicIdTypeFlag();
    uint16_t topicId = payload->getTopicId();

    if (topicIdType == TopicIdType::PRE_DEFINED_TOPIC_ID) {
        // remove subscription if the predefined topic ID exists
        auto it = idsToTopics.find(topicId);
        if (it != idsToTopics.end() && it->second.topicIdType == topicIdType) {
            deleteSubscriptionIfExists(srcAddress, srcPort, topicId);
        }
    }
    else {
        std::string topicName = StringHelper::sanitizeSpaces(payload->getTopicName());
        // check and remove subscription if a valid topic name exists
        if (MqttSNApp::isMinTopicLength(topicName.length())) {
            auto it = topicsToIds.find(StringHelper::base64Encode(topicName));
            if (it != topicsToIds.end()) {
                deleteSubscriptionIfExists(srcAddress, srcPort, it->second);
            }
        }
    }

    // send ACK message
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::UNSUBACK, payload->getMsgId());
}

void MqttSNServer::processRegAck(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNMsgIdWithTopicIdPlus>();

    // check if the ACK is correct; exit if not
    if (!processRegistrationAck(payload->getMsgId())) {
        return;
    }

    uint16_t topicId = payload->getTopicId();

    if (topicId == 0) {
        throw omnetpp::cRuntimeError("Unexpected error: Invalid topic ID");
    }

    if (payload->getReturnCode() != ReturnCode::ACCEPTED) {
        // remove subscription if exists
        deleteSubscriptionIfExists(srcAddress, srcPort, topicId);
        return;
    }

    // handle ACCEPTED return code
    SubscriberTopicInfo* subscriberTopicInfo = getSubscriberTopicInfo(srcAddress, srcPort, topicId);

    if (subscriberTopicInfo == nullptr) {
        // delayed topic registration; simply return
        return;
    }

    // update topic registration status
    subscriberTopicInfo->isRegistered = true;
}

void MqttSNServer::processPubAck(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNMsgIdWithTopicIdPlus>();
    uint16_t msgId = payload->getMsgId();

    if (msgId > 0) {
        // check if the ACK is correct; exit if not
        if (!processRequestAck(msgId, MsgType::PUBLISH)) {
            return;
        }
    }

    // now process and analyze message content as needed
    ReturnCode returnCode = payload->getReturnCode();

    if (returnCode == ReturnCode::REJECTED_INVALID_TOPIC_ID) {
        // remove subscription if exists
        deleteSubscriptionIfExists(srcAddress, srcPort, payload->getTopicId());
        return;
    }

    if (returnCode != ReturnCode::ACCEPTED) {
        throw omnetpp::cRuntimeError("Unexpected error: Invalid return code");
    }
}

void MqttSNServer::processPubRec(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();
    uint16_t msgId = payload->getMsgId();

    std::map<uint16_t, RequestInfo>::iterator requestIt;
    std::set<uint16_t>::iterator requestIdIt;

    // check if the ACK is valid; exit if not
    if (!isValidRequest(msgId, MsgType::PUBLISH, requestIt, requestIdIt)) {
        return;
    }

    // send PUBlish RELease
    sendBaseWithMsgId(srcAddress, srcPort, MsgType::PUBREL, msgId);

    // update the request
    requestIt->second.requestTime = getClockTime();
    requestIt->second.retransmissionCounter = 0;
    requestIt->second.messageType = MsgType::PUBREL;
}

void MqttSNServer::processPubComp(inet::Packet* pk, const inet::L3Address& srcAddress, const int& srcPort)
{
    const auto& payload = pk->peekData<MqttSNBaseWithMsgId>();

    // check if the ACK is correct; exit if not
    if (!processRequestAck(payload->getMsgId(), MsgType::PUBREL)) {
        return;
    }
}

void MqttSNServer::sendAdvertise()
{
    const auto& payload = inet::makeShared<MqttSNAdvertise>();
    payload->setMsgType(MsgType::ADVERTISE);
    payload->setGwId(gatewayId);
    payload->setDuration(advertiseInterval);
    payload->setChunkLength(inet::B(payload->getLength()));

    inet::Packet* packet = new inet::Packet("AdvertisePacket");
    packet->insertAtBack(payload);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, inet::L3Address(par("broadcastAddress")), par("destPort"));
}

void MqttSNServer::sendBaseWithReturnCode(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, ReturnCode returnCode)
{
    const auto& payload = inet::makeShared<MqttSNBaseWithReturnCode>();
    payload->setMsgType(msgType);
    payload->setReturnCode(returnCode);
    payload->setChunkLength(inet::B(payload->getLength()));

    std::string packetName;

    switch(msgType) {
        case MsgType::CONNACK:
            packetName = "ConnAckPacket";
            break;

        case MsgType::WILLTOPICRESP:
            packetName = "WillTopicRespPacket";
            break;

        case MsgType::WILLMSGRESP:
            packetName = "WillMsgRespPacket";
            break;

        default:
            packetName = "BaseWithReturnCodePacket";
    }

    inet::Packet* packet = new inet::Packet(packetName.c_str());
    packet->insertAtBack(payload);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendMsgIdWithTopicIdPlus(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, uint16_t topicId,
                                            uint16_t msgId, ReturnCode returnCode)
{
    inet::Packet* packet = PacketHelper::getMsgIdWithTopicIdPlusPacket(msgType, topicId, msgId, returnCode);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendBaseWithMsgId(const inet::L3Address& destAddress, const int& destPort, MsgType msgType, uint16_t msgId)
{
    inet::Packet* packet = PacketHelper::getBaseWithMsgIdPacket(msgType, msgId);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendSubAck(const inet::L3Address& destAddress, const int& destPort, QoS qosFlag, uint16_t topicId, uint16_t msgId,
                              ReturnCode returnCode)
{
    const auto& payload = inet::makeShared<MqttSNSubAck>();
    payload->setMsgType(MsgType::SUBACK);
    payload->setQoSFlag(qosFlag);
    payload->setTopicId(topicId);
    payload->setMsgId(msgId);
    payload->setReturnCode(returnCode);
    payload->setChunkLength(inet::B(payload->getLength()));

    inet::Packet* packet = new inet::Packet("SubAckPacket");
    packet->insertAtBack(payload);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendRegister(const inet::L3Address& destAddress, const int& destPort, uint16_t topicId, uint16_t msgId,
                                const std::string& topicName)
{
    inet::Packet* packet = PacketHelper::getRegisterPacket(topicId, msgId, topicName);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::sendPublish(const inet::L3Address& destAddress, const int& destPort, bool dupFlag, QoS qosFlag, bool retainFlag,
                               TopicIdType topicIdTypeFlag, uint16_t topicId, uint16_t msgId, const std::string& data, const TagInfo& tagInfo)
{
    inet::Packet* packet = PacketHelper::getPublishPacket(dupFlag, qosFlag, retainFlag, topicIdTypeFlag, topicId, msgId, data, tagInfo);
    MqttSNApp::corruptPacket(packet, MqttSNApp::packetBER);

    MqttSNApp::socket.sendTo(packet, destAddress, destPort);
}

void MqttSNServer::handleAdvertiseEvent()
{
    sendAdvertise();

    scheduleClockEventAfter(advertiseInterval, advertiseEvent);
}

void MqttSNServer::handleActiveClientsCheckEvent()
{
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        ClientInfo& clientInfo = it->second;

        // check if the client is ACTIVE and if the elapsed time from last received message is beyond the keep alive duration
        if (clientInfo.currentState == ClientState::ACTIVE &&
            (getClockTime() - clientInfo.lastReceivedMsgTime) > clientInfo.keepAliveDuration) {

            if (clientInfo.sentPingReq) {
                // change the expired client state and activate the will feature
                clientInfo.currentState = ClientState::LOST;
                // will feature activation; to be implemented
            }
            else {
                // send a solicitation PINGREQ to the expired client
                const std::pair<inet::L3Address, int> clientKey = it->first;

                MqttSNApp::sendPingReq(clientKey.first, clientKey.second);
                clientInfo.sentPingReq = true;
            }
        }
    }

    scheduleClockEventAfter(activeClientsCheckInterval, activeClientsCheckEvent);
}

void MqttSNServer::handleAsleepClientsCheckEvent()
{
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        ClientInfo& clientInfo = it->second;

        // check if the client is ASLEEP and if the elapsed time from last received message is beyond the sleep duration
        if (clientInfo.currentState == ClientState::ASLEEP &&
            (getClockTime() - clientInfo.lastReceivedMsgTime) > clientInfo.sleepDuration) {

            // change the expired client state and activate the will feature
            clientInfo.currentState = ClientState::LOST;
            // will feature activation; to be implemented
        }
    }

    scheduleClockEventAfter(asleepClientsCheckInterval, asleepClientsCheckEvent);
}

void MqttSNServer::handlePendingRetainCheckEvent()
{
    for (auto it = pendingRetainMessages.begin(); it != pendingRetainMessages.end();) {
        // extract the information
        std::pair<inet::L3Address, int> subscriber = it->first;
        const MessageInfo& messageInfo = it->second;

        // send the retained message to the subscriber with appropriate QoS
        addAndSendPublishRequest(subscriber.first, subscriber.second, messageInfo, messageInfo.qos, 0, messageInfo.topicId);

        // remove the subscriber after sending the message
        it = pendingRetainMessages.erase(it);
    }

    scheduleClockEventAfter(pendingRetainCheckInterval, pendingRetainCheckEvent);
}

void MqttSNServer::handleRequestsCheckEvent()
{
    // structure to store allocated objects for future deallocation
    std::vector<MessageInfo*> allocatedObjects;

    // iterate through the requests
    for (auto requestIt = requests.begin(); requestIt != requests.end();) {
        // find the same request ID in the set
        auto requestIdIt = requestIds.find(requestIt->first);
        if (requestIdIt == requestIds.end()) {
            deleteRequest(requestIt, requestIdIt);
            continue;
        }

        RequestInfo& requestInfo = requestIt->second;

        // retrieve subscriber address and port
        const inet::L3Address& subscriberAddress = requestInfo.subscriberAddress;
        const int& subscriberPort = requestInfo.subscriberPort;

        // get client information for the subscriber
        ClientInfo* clientInfo = getSubscriberClientInfo(subscriberAddress, subscriberPort);

        // check if the subscriber is in an ACTIVE or AWAKE state
        if (clientInfo->currentState != ClientState::ACTIVE && clientInfo->currentState != ClientState::AWAKE) {
            ++requestIt;
            continue;
        }

        // get a message info pointer for regular or retained messages; memory allocation occurs for retained messages
        MessageInfo* messageInfo = getRequestMessageInfo(requestInfo, allocatedObjects);

        // check for an existing subscription
        std::pair<uint16_t, QoS> subscriptionKey;
        if (!findSubscription(subscriberAddress, subscriberPort, messageInfo->topicId, subscriptionKey)) {
            deleteRequest(requestIt, requestIdIt);
            continue;
        }

        // check if the subscriber is in the ACTIVE state and if the topic is registered for the subscriber
        if (clientInfo->currentState == ClientState::ACTIVE &&
            !isTopicRegisteredForSubscriber(subscriberAddress, subscriberPort, messageInfo->topicId)) {

            // handle unregistered topic: initiate subscriber registration; the request will be processed later
            manageRegistration(subscriberAddress, subscriberPort, messageInfo->topicId);

            ++requestIt;
            continue;
        }

        QoS resultQoS;

        if (requestInfo.messageType == MsgType::PUBLISH) {
            // calculate the minimum QoS level between subscription QoS and original PUBLISH QoS
            resultQoS = NumericHelper::minQoS(subscriptionKey.second, messageInfo->qos);

            if (resultQoS == QoS::QOS_MINUS_ONE || resultQoS == QoS::QOS_ZERO) {
                // send a PUBLISH message with QoS -1 or QoS 0 to the subscriber
                sendPublish(subscriberAddress, subscriberPort, messageInfo->dup, resultQoS, messageInfo->retain,
                            messageInfo->topicIdType, messageInfo->topicId, 0, messageInfo->data, messageInfo->tagInfo);

                deleteRequest(requestIt, requestIdIt);
                continue;
            }

            if (requestInfo.sendAtLeastOnce) {
                // send a PUBLISH message with QoS 1 or QoS 2 to the subscriber
                sendPublish(subscriberAddress, subscriberPort, messageInfo->dup, resultQoS, messageInfo->retain,
                            messageInfo->topicIdType, messageInfo->topicId, requestIt->first, messageInfo->data, messageInfo->tagInfo);

                // update request information
                requestInfo.sendAtLeastOnce = false;
                requestInfo.requestTime = getClockTime();

                ++requestIt;
                continue;
            }
        }

        // check if the elapsed time from last received message is beyond the retransmission duration
        if ((getClockTime() - requestInfo.requestTime) > MqttSNApp::retransmissionInterval) {
            // check if the number of retries equals the threshold
            if (requestInfo.retransmissionCounter >= MqttSNApp::retransmissionCounter) {
                deleteRequest(requestIt, requestIdIt);
                continue;
            }

            resultQoS = NumericHelper::minQoS(subscriptionKey.second, messageInfo->qos);

            if (requestInfo.messageType == MsgType::PUBLISH) {
                // send a PUBLISH message with QoS 1 or QoS 2 to the subscriber
                sendPublish(subscriberAddress, subscriberPort, true, resultQoS, messageInfo->retain,
                            messageInfo->topicIdType, messageInfo->topicId, requestIt->first, messageInfo->data, messageInfo->tagInfo);
            }
            else if (requestInfo.messageType == MsgType::PUBREL) {
                // send PUBlish RELease
                sendBaseWithMsgId(subscriberAddress, subscriberPort, MsgType::PUBREL, requestIt->first);
            }

            // update request information
            requestInfo.retransmissionCounter++;
            requestInfo.requestTime = getClockTime();

            MqttSNApp::serversRetransmissions++;
        }

        ++requestIt;
    }

    // deallocate objects
    deleteAllocatedMessages(allocatedObjects);

    scheduleClockEventAfter(requestsCheckInterval, requestsCheckEvent);
}

void MqttSNServer::handleRegistrationsCheckEvent()
{
    // iterate through the registrations
    for (auto registrationIt = registrations.begin(); registrationIt != registrations.end();) {
        // find the same registration ID in the set
        auto registrationIdIt = registrationIds.find(registrationIt->first);
        if (registrationIdIt == registrationIds.end()) {
            deleteRegistration(registrationIt, registrationIdIt);
            continue;
        }

        RegisterInfo& registerInfo = registrationIt->second;

        // check if the elapsed time from last received message is beyond the retransmission duration
        if ((getClockTime() - registerInfo.requestTime) > MqttSNApp::retransmissionInterval) {
            // check if the number of retries equals the threshold
            if (registerInfo.retransmissionCounter >= MqttSNApp::retransmissionCounter) {
                deleteRegistration(registrationIt, registrationIdIt);
                continue;
            }

            sendRegister(registerInfo.subscriberAddress, registerInfo.subscriberPort, registerInfo.topicId,
                         registrationIt->first, registerInfo.topicName);

            // update the registration
            registerInfo.retransmissionCounter++;
            registerInfo.requestTime = getClockTime();

            MqttSNApp::serversRetransmissions++;
        }

        ++registrationIt;
    }

    scheduleClockEventAfter(registrationsCheckInterval, registrationsCheckEvent);
}

void MqttSNServer::handleAwakenSubscriberCheckEvent(omnetpp::cMessage* msg)
{
    // extract subscriber address and port from the message
    inet::L3Address subscriberAddress = inet::L3Address(msg->par("subscriberAddress").stringValue());
    int subscriberPort = msg->par("subscriberPort").longValue();

    std::pair<inet::L3Address, int> key = std::make_pair(subscriberAddress, subscriberPort);

    // search for the subscriber in the structure
    auto it = subscribers.find(key);
    if (it == subscribers.end()) {
        throw omnetpp::cRuntimeError("Subscriber not found while processing the check event");
    }

    SubscriberInfo& subscriberInfo = it->second;

    // check if the elapsed time since the start of the scheduled event is within the threshold
    if ((getClockTime() - subscriberInfo.awakenSubscriberCheckStartTime) <=
        MqttSNApp::retransmissionCounter * MqttSNApp::retransmissionInterval) {

        // search if there is at least one pending request for the subscriber in AWAKE state
        for (const auto& request : requests) {
            if (request.second.subscriberAddress == subscriberAddress && request.second.subscriberPort == subscriberPort) {
                // if there is a pending request, reschedule and check again next time
                scheduleClockEventAfter(awakenSubscriberCheckInterval, subscriberInfo.awakenSubscriberCheckEvent);
                return;
            }
        }
    }

    // no pending requests found for the subscriber; set its state to ASLEEP and respond with PINGRESP
    ClientInfo* clientInfo = getSubscriberClientInfo(subscriberAddress, subscriberPort);
    clientInfo->currentState = ClientState::ASLEEP;

    // send PINGRESP message to the subscriber
    MqttSNApp::sendBase(subscriberAddress, subscriberPort, MsgType::PINGRESP);

    // deallocate clock event object
    delete subscriberInfo.awakenSubscriberCheckEvent;
    subscriberInfo.awakenSubscriberCheckEvent = nullptr;
}

void MqttSNServer::handleMessagesClearEvent()
{
    // iterate through messages
    for (auto messageIt = messages.begin(); messageIt != messages.end();) {
        // find the same message ID in the set
        auto messageIdIt = messageIds.find(messageIt->first);
        if (messageIdIt == messageIds.end()) {
            throw omnetpp::cRuntimeError("Mismatch between message structures during message clearance");
        }

        bool isUsed = false;

        // check if the message is used in at least one request
        for (auto requestIt = requests.begin(); requestIt != requests.end(); ++requestIt) {
            if (requestIt->second.messagesKey == messageIt->first) {
                isUsed = true;
                break;
            }
        }

        // if the message is not used, remove it
        if (!isUsed) {
            deleteMessage(messageIt, messageIdIt);
            continue;
        }

        ++messageIt;
    }

    scheduleClockEventAfter(messagesClearInterval, messagesClearEvent);
}

void MqttSNServer::cleanClientSession(const inet::L3Address& clientAddress, const int& clientPort, ClientType clientType)
{
    if (clientType == ClientType::PUBLISHER) {
        PublisherInfo* publisherInfo = getPublisherInfo(clientAddress, clientPort);
        if (publisherInfo == nullptr) {
            throw omnetpp::cRuntimeError("Publisher not found during the clean session operation");
        }

        // delete will-related data for the publisher
        publisherInfo->will = false;
        publisherInfo->willQoS = QoS::QOS_ZERO;
        publisherInfo->willRetain = false;
        publisherInfo->willTopic = "";
        publisherInfo->willMsg = "";

        return;
    }

    // handle the case when the client is identified as a subscriber
    SubscriberInfo* subscriberInfo = getSubscriberInfo(clientAddress, clientPort);
    if (subscriberInfo == nullptr) {
        throw omnetpp::cRuntimeError("Subscriber not found during the clean session operation");
    }

    // delete all subscriptions for the subscriber
    for (auto& topic : subscriberInfo->subscriberTopics) {
        deleteSubscriptionIfExists(clientAddress, clientPort, topic.first);
    }
}

void MqttSNServer::updateClientType(ClientInfo* clientInfo, ClientType clientType)
{
    // update the client type if the current client type is CLIENT
    if (clientInfo->clientType == ClientType::CLIENT) {
        clientInfo->clientType = clientType;
    }
}

ClientInfo* MqttSNServer::addNewClient(const inet::L3Address& clientAddress, const int& clientPort)
{
    // create a pair for client address and port
    std::pair<inet::L3Address, int> clientKey = std::make_pair(clientAddress, clientPort);

    // insert a new default client
    ClientInfo clientInfo;
    clients[clientKey] = clientInfo;

    return &clients[clientKey];
}

ClientInfo* MqttSNServer::getClientInfo(const inet::L3Address& clientAddress, const int& clientPort)
{
    // check if the client with the specified address and port is present in the data structure
    auto clientIterator = clients.find(std::make_pair(clientAddress, clientPort));
    if (clientIterator != clients.end()) {
        return &clientIterator->second;
    }

    return nullptr;
}

PublisherInfo* MqttSNServer::getPublisherInfo(const inet::L3Address& publisherAddress, const int& publisherPort, bool insertIfNotFound)
{
    // create a pair for publisher address and port
    std::pair<inet::L3Address, int> publisherKey = std::make_pair(publisherAddress, publisherPort);

    // check if the publisher with the specified address and port is present in the data structure
    auto publisherIterator = publishers.find(publisherKey);
    if (publisherIterator != publishers.end()) {
        return &publisherIterator->second;
    }

    if (insertIfNotFound) {
        // insert a new default publisher
        PublisherInfo publisherInfo;
        publishers[publisherKey] = publisherInfo;

        return &publishers[publisherKey];
    }

    return nullptr;
}

void MqttSNServer::fillWithPredefinedTopics()
{
    // retrieve predefined topics and their IDs
    std::map<std::string, uint16_t> predefinedTopics = MqttSNApp::getPredefinedTopics();

    for (const auto& topic : predefinedTopics) {
        addNewTopic(topic.first, topic.second, TopicIdType::PRE_DEFINED_TOPIC_ID);
    }
}

void MqttSNServer::addNewTopic(const std::string& topicName, uint16_t topicId, TopicIdType topicIdType)
{
    TopicInfo topicInfo;
    topicInfo.topicName = topicName;
    topicInfo.topicIdType = topicIdType;

    // add the new topic in the data structures
    topicsToIds[topicName] = topicId;
    idsToTopics[topicId] = topicInfo;
    topicIds.insert(topicId);
}

void MqttSNServer::checkTopicsToIds(const std::string& topicName, uint16_t topicId)
{
    if (topicId != getTopicByName(topicName)) {
        throw omnetpp::cRuntimeError("Mismatch in topic IDs");
    }
}

uint16_t MqttSNServer::getTopicByName(const std::string& topicName)
{
    auto it = topicsToIds.find(topicName);
    if (it == topicsToIds.end()) {
        throw omnetpp::cRuntimeError("Topic name not found in the structure");
    }

    return it->second;
}

TopicInfo MqttSNServer::getTopicById(uint16_t topicId)
{
    auto it = idsToTopics.find(topicId);
    if (it == idsToTopics.end()) {
        throw omnetpp::cRuntimeError("Topic ID not found in the structure");
    }

    return it->second;
}

TopicIdType MqttSNServer::getTopicIdType(uint16_t topicLength)
{
    if (topicLength == Length::TWO_OCTETS) {
        return TopicIdType::SHORT_TOPIC_ID;
    }
    else if (topicLength > Length::TWO_OCTETS) {
        return TopicIdType::NORMAL_TOPIC_ID;
    }

    throw omnetpp::cRuntimeError("Invalid topic length");
}

void MqttSNServer::addNewRetainMessage(uint16_t topicId, bool dup, QoS qos, TopicIdType topicIdType, const std::string& data)
{
    // store message as retained for the topic
    RetainMessageInfo retainMessageInfo;
    retainMessageInfo.dup = dup;
    retainMessageInfo.qos = qos;
    retainMessageInfo.topicIdType = topicIdType;
    retainMessageInfo.data = data;

    retainMessages[topicId] = retainMessageInfo;
    retainMessageIds.insert(topicId);
}

void MqttSNServer::addNewPendingRetainMessage(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId, QoS qos)
{
    // check for retained message on the subscribed topic
    auto retainMsgIt = retainMessages.find(topicId);
    if (retainMsgIt != retainMessages.end()) {
        // retrieve retained message information
        const RetainMessageInfo& retainMessageInfo = retainMsgIt->second;

        MessageInfo messageInfo;
        messageInfo.topicId = topicId;
        messageInfo.topicIdType = retainMessageInfo.topicIdType;
        messageInfo.dup = retainMessageInfo.dup;
        messageInfo.retain = true;
        messageInfo.data = retainMessageInfo.data;

        // calculate the minimum QoS level between subscription QoS and original PUBLISH QoS
        messageInfo.qos = NumericHelper::minQoS(qos, retainMessageInfo.qos);

        // store the pending retain message for the subscriber
        pendingRetainMessages[std::make_pair(subscriberAddress, subscriberPort)] = messageInfo;
    }
}

void MqttSNServer::addNewMessage(const MessageInfo& messageInfo)
{
    // set new available message ID if possible; otherwise, throw an exception
    MqttSNApp::getNewIdentifier(messageIds, currentMessageId,
                                "Failed to assign a new message ID. All available message IDs are in use");

    // add the new message in the data structures
    messages[currentMessageId] = messageInfo;
    messageIds.insert(currentMessageId);
}

void MqttSNServer::addAndMarkMessage(const MessageInfo& messageInfo, bool& isMessageAdded)
{
    addNewMessage(messageInfo);
    isMessageAdded = true;
}

void MqttSNServer::deleteMessage(std::map<uint16_t, MessageInfo>::iterator& messageIt, std::set<uint16_t>::iterator& messageIdIt)
{
    // remove the message from both structures
    messageIt = messages.erase(messageIt);
    messageIdIt = messageIds.erase(messageIdIt);
}

void MqttSNServer::deleteAllocatedMessages(const std::vector<MessageInfo*>& messages)
{
    // delete objects pointed to by the pointers in the vector
    for (auto message : messages) {
        delete message;
    }
}

MessageInfo* MqttSNServer::getRequestMessageInfo(const RequestInfo& requestInfo, std::vector<MessageInfo*>& allocatedObjects)
{
    MessageInfo* messageInfo = nullptr;

    if (requestInfo.messagesKey > 0) {
        // check if the key exists in the messages map
        auto messageIt = messages.find(requestInfo.messagesKey);
        if (messageIt != messages.end()) {
            messageInfo = &messageIt->second;
        }

        return messageInfo;
    }
    else if (requestInfo.retainMessagesKey > 0) {
        // check if the key exists in the retain messages map
        auto retainMessageIt = retainMessages.find(requestInfo.retainMessagesKey);
        if (retainMessageIt != retainMessages.end()) {
            const RetainMessageInfo& retainMessageInfo = retainMessageIt->second;

            // allocate memory for a new object
            messageInfo = new MessageInfo;

            // populate the fields of the new object
            messageInfo->topicId = requestInfo.retainMessagesKey;
            messageInfo->topicIdType = retainMessageInfo.topicIdType;
            messageInfo->dup = retainMessageInfo.dup;
            messageInfo->qos = retainMessageInfo.qos;
            messageInfo->retain = true;
            messageInfo->data = retainMessageInfo.data;

            // add the object pointer to the vector for future deallocation
            allocatedObjects.push_back(messageInfo);
        }

        return messageInfo;
    }

    throw omnetpp::cRuntimeError("Expecting at least one valid message key");
}

void MqttSNServer::dispatchPublishToSubscribers(const MessageInfo& messageInfo)
{
    // set to track whether a new message needs to be added
    bool isMessageAdded = false;

    // keys with the same topic ID
    std::set<std::pair<uint16_t, QoS>> keys = getSubscriptionKeysByTopicId(messageInfo.topicId);

    // iterate for each QoS
    for (const auto& key : keys) {
        // check if the subscription exists for the given key
        auto subscriptionIt = subscriptions.find(key);
        if (subscriptionIt != subscriptions.end()) {
            // calculate the minimum QoS level between subscription QoS and incoming PUBLISH QoS
            QoS resultQoS = NumericHelper::minQoS(key.second, messageInfo.qos);

            for (const auto& subscriber : subscriptionIt->second) {
                // retrieve subscriber address and port
                const inet::L3Address& subscriberAddress = subscriber.first;
                const int& subscriberPort = subscriber.second;

                // get client information for the subscriber
                ClientInfo* clientInfo = getSubscriberClientInfo(subscriberAddress, subscriberPort);

                // check subscriber state and handle accordingly
                switch (clientInfo->currentState) {
                    case ClientState::ACTIVE:
                        // check if the subscriber is registered for the topic and take appropriate action
                        processRequestForActiveSubscriber(subscriberAddress, subscriberPort, messageInfo, resultQoS, isMessageAdded);
                        break;

                    case ClientState::AWAKE:
                        // registered topic; send request directly, keeping only QoS 1 and 2 requests
                        processRequest(subscriberAddress, subscriberPort, messageInfo, resultQoS, isMessageAdded);
                        break;

                    case ClientState::ASLEEP:
                        // keep the request to be processed later
                        bufferRequest(subscriberAddress, subscriberPort, messageInfo, isMessageAdded);
                        break;

                    default:
                        break;
                }
            }
        }
    }
}

void MqttSNServer::processRequestForActiveSubscriber(const inet::L3Address& subscriberAddress, int subscriberPort,
                                                     const MessageInfo& messageInfo, QoS resultQoS, bool& isMessageAdded)
{
    if (isTopicRegisteredForSubscriber(subscriberAddress, subscriberPort, messageInfo.topicId)) {
        // registered topic; send request directly, keeping only QoS 1 and QoS 2 requests
        processRequest(subscriberAddress, subscriberPort, messageInfo, resultQoS, isMessageAdded);
        return;
    }

    // unregistered topic; manage the registration for the subscriber
    manageRegistration(subscriberAddress, subscriberPort, messageInfo.topicId);

    // keep the request to be processed later
    bufferRequest(subscriberAddress, subscriberPort, messageInfo, isMessageAdded);
}

void MqttSNServer::processRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, const MessageInfo& messageInfo,
                                  QoS resultQoS, bool& isMessageAdded)
{
    // add a request message if not added and if QoS is one or two
    if (!isMessageAdded && (resultQoS == QoS::QOS_ONE || resultQoS == QoS::QOS_TWO)) {
        addAndMarkMessage(messageInfo, isMessageAdded);
    }

    // process the PUBLISH request to the subscriber
    addAndSendPublishRequest(subscriberAddress, subscriberPort, messageInfo, resultQoS, currentMessageId);
}

void MqttSNServer::bufferRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, const MessageInfo& messageInfo,
                                 bool& isMessageAdded)
{
    // add a request message if not added
    if (!isMessageAdded) {
        addAndMarkMessage(messageInfo, isMessageAdded);
    }

    // add a new PUBLISH request to be processed later
    addNewRequest(subscriberAddress, subscriberPort, MsgType::PUBLISH, true, currentMessageId, 0);
}

void MqttSNServer::addAndSendPublishRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, const MessageInfo& messageInfo,
                                            QoS resultQoS, uint16_t messagesKey, uint16_t retainMessagesKey)
{
    if (resultQoS == QoS::QOS_MINUS_ONE || resultQoS == QoS::QOS_ZERO) {
        // send a PUBLISH message with QoS -1 or QoS 0 to the subscriber
        sendPublish(subscriberAddress, subscriberPort, messageInfo.dup, resultQoS, messageInfo.retain,
                    messageInfo.topicIdType, messageInfo.topicId, 0, messageInfo.data, messageInfo.tagInfo);

        // continue to the next subscriber
        return;
    }

    addNewRequest(subscriberAddress, subscriberPort, MsgType::PUBLISH, false, messagesKey, retainMessagesKey);

    // send a PUBLISH message with QoS 1 or QoS 2 to the subscriber
    sendPublish(subscriberAddress, subscriberPort, messageInfo.dup, resultQoS, messageInfo.retain,
                messageInfo.topicIdType, messageInfo.topicId, currentRequestId, messageInfo.data, messageInfo.tagInfo);
}

void MqttSNServer::addNewRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort, MsgType messageType, bool sendAtLeastOnce,
                                 uint16_t messagesKey, uint16_t retainMessagesKey)
{
    // check for valid message types; throw an exception if invalid
    if (messageType != MsgType::PUBLISH && messageType != MsgType::PUBREL) {
        throw omnetpp::cRuntimeError("Invalid message type provided while adding the new request");
    }

    // set new available request ID if possible; otherwise, throw an exception
    MqttSNApp::getNewIdentifier(requestIds, currentRequestId,
                                "Failed to assign a new request ID. All available request IDs are in use");

    RequestInfo requestInfo;
    requestInfo.requestTime = getClockTime();
    requestInfo.subscriberAddress = subscriberAddress;
    requestInfo.subscriberPort = subscriberPort;
    requestInfo.messageType = messageType;
    requestInfo.sendAtLeastOnce = sendAtLeastOnce;

    if (messagesKey > 0) {
        requestInfo.messagesKey = messagesKey;
    }

    if (retainMessagesKey > 0) {
        requestInfo.retainMessagesKey = retainMessagesKey;
    }

    // add the new request in the data structures
    requests[currentRequestId] = requestInfo;
    requestIds.insert(currentRequestId);
}

void MqttSNServer::deleteRequest(std::map<uint16_t, RequestInfo>::iterator& requestIt, std::set<uint16_t>::iterator& requestIdIt)
{
    // remove the request from both structures
    requestIt = requests.erase(requestIt);
    requestIdIt = requestIds.erase(requestIdIt);
}

bool MqttSNServer::isValidRequest(uint16_t requestId, MsgType messageType, std::map<uint16_t, RequestInfo>::iterator& requestIt,
                                  std::set<uint16_t>::iterator& requestIdIt)
{
    // search for the request ID in the map
    requestIt = requests.find(requestId);
    if (requestIt == requests.end()) {
        return false;
    }

    // check if the request message type matches
    if (requestIt->second.messageType != messageType) {
        return false;
    }

    // search for the request ID in the set
    requestIdIt = requestIds.find(requestId);
    if (requestIdIt == requestIds.end()) {
        return false;
    }

    return true;
}

bool MqttSNServer::processRequestAck(uint16_t requestId, MsgType messageType)
{
    std::map<uint16_t, RequestInfo>::iterator requestIt;
    std::set<uint16_t>::iterator requestIdIt;

    // check if the request is valid and retrieve iterators
    if (!isValidRequest(requestId, messageType, requestIt, requestIdIt)) {
        return false;
    }

    deleteRequest(requestIt, requestIdIt);
    return true;
}

void MqttSNServer::manageRegistration(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId)
{
    std::string topicName = StringHelper::base64Decode(getTopicById(topicId).topicName);

    // check for topics structure alignment by verifying the encoded topic name and its corresponding topic ID
    checkTopicsToIds(StringHelper::base64Encode(topicName), topicId);

    // add a new registration entry
    addNewRegistration(subscriberAddress, subscriberPort, topicName, topicId);

    sendRegister(subscriberAddress, subscriberPort, topicId, currentRegistrationId, topicName);
}

void MqttSNServer::addNewRegistration(const inet::L3Address& subscriberAddress, const int& subscriberPort, const std::string& topicName,
                                      uint16_t topicId)
{
    // set new available registration ID if possible; otherwise, throw an exception
    MqttSNApp::getNewIdentifier(registrationIds, currentRegistrationId,
                                "Failed to assign a new registration ID. All available registration IDs are in use");

    RegisterInfo registerInfo;
    registerInfo.requestTime = getClockTime();
    registerInfo.subscriberAddress = subscriberAddress;
    registerInfo.subscriberPort = subscriberPort;
    registerInfo.topicName = topicName;
    registerInfo.topicId = topicId;

    // add the new registration in the data structures
    registrations[currentRegistrationId] = registerInfo;
    registrationIds.insert(currentRegistrationId);
}

void MqttSNServer::deleteRegistration(std::map<uint16_t, RegisterInfo>::iterator& registrationIt, std::set<uint16_t>::iterator& registrationIdIt)
{
    // remove the registration from both structures
    registrationIt = registrations.erase(registrationIt);
    registrationIdIt = registrationIds.erase(registrationIdIt);
}

bool MqttSNServer::processRegistrationAck(uint16_t registrationId)
{
    std::map<uint16_t, RegisterInfo>::iterator registrationIt;
    std::set<uint16_t>::iterator registrationIdIt;

    // search for the registration ID in the map
    registrationIt = registrations.find(registrationId);
    if (registrationIt == registrations.end()) {
        return false;
    }

    // search for the registration ID in the set
    registrationIdIt = registrationIds.find(registrationId);
    if (registrationIdIt == registrationIds.end()) {
        return false;
    }

    deleteRegistration(registrationIt, registrationIdIt);
    return true;
}

void MqttSNServer::setAllSubscriberTopics(const inet::L3Address& subscriberAddress, const int& subscriberPort, bool isRegistered,
                                          bool skipPredefinedTopics)
{
    SubscriberInfo* subscriberInfo = getSubscriberInfo(subscriberAddress, subscriberPort);
    if (subscriberInfo == nullptr) {
        // exit if the subscriber is not found
        return;
    }

    // set the registration status for all the subscribed topics
    for (auto& topic : subscriberInfo->subscriberTopics) {
        // skip predefined topics if required
        if (skipPredefinedTopics && topic.second.topicIdType == TopicIdType::PRE_DEFINED_TOPIC_ID) {
            continue;
        }

        // update the registration status
        topic.second.isRegistered = isRegistered;
    }
}

void MqttSNServer::handleSubscriberPingRequest(const inet::L3Address& subscriberAddress, const int& subscriberPort)
{
    // retrieve subscriber information
    SubscriberInfo* subscriberInfo = getSubscriberInfo(subscriberAddress, subscriberPort);
    if (subscriberInfo == nullptr) {
        throw omnetpp::cRuntimeError("Subscriber not found during ping request processing");
    }

    if (subscriberInfo->awakenSubscriberCheckEvent == nullptr) {
        // control event for subscriber pending requests
        manageAwakenSubscriberEvent(subscriberAddress, subscriberPort, subscriberInfo);
    }
}

void MqttSNServer::manageAwakenSubscriberEvent(const inet::L3Address& srcAddress, const int& srcPort,
                                               SubscriberInfo* subscriberInfo)
{
    subscriberInfo->awakenSubscriberCheckEvent = new inet::ClockEvent("awakenSubscriberCheckTimer");

    // add parameters
    subscriberInfo->awakenSubscriberCheckEvent->addPar("isAwakenSubscriberCheckEvent");
    subscriberInfo->awakenSubscriberCheckEvent->addPar("subscriberAddress").setStringValue(srcAddress.str().c_str());
    subscriberInfo->awakenSubscriberCheckEvent->addPar("subscriberPort").setLongValue(srcPort);

    // record the start time of the schedule
    subscriberInfo->awakenSubscriberCheckStartTime = getClockTime();

    // schedule the control event after a specified interval
    scheduleClockEventAfter(awakenSubscriberCheckInterval, subscriberInfo->awakenSubscriberCheckEvent);
}

bool MqttSNServer::isTopicRegisteredForSubscriber(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId)
{
    SubscriberTopicInfo* subscriberTopicInfo = getSubscriberTopicInfo(subscriberAddress, subscriberPort, topicId);

    if (subscriberTopicInfo == nullptr) {
        throw omnetpp::cRuntimeError("Topic not found for the subscriber");
    }

    // return the registration status of the topic for the subscriber
    return subscriberTopicInfo->isRegistered;
}

SubscriberInfo* MqttSNServer::getSubscriberInfo(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                                bool insertIfNotFound)
{
    // create a pair for subscriber address and port
    std::pair<inet::L3Address, int> subscriberKey = std::make_pair(subscriberAddress, subscriberPort);

    // check if the subscriber with the specified address and port is present in the data structure
    auto subscriberIterator = subscribers.find(subscriberKey);
    if (subscriberIterator != subscribers.end()) {
        return &subscriberIterator->second;
    }

    if (insertIfNotFound) {
        // insert a new default subscriber
        SubscriberInfo subscriberInfo;
        subscribers[subscriberKey] = subscriberInfo;

        return &subscribers[subscriberKey];
    }

    return nullptr;
}

SubscriberTopicInfo* MqttSNServer::getSubscriberTopicInfo(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId)
{
    SubscriberInfo* subscriberInfo = getSubscriberInfo(subscriberAddress, subscriberPort);
    if (subscriberInfo == nullptr) {
        throw omnetpp::cRuntimeError("Subscriber not found for topic retrieval");
    }

    // obtain the subscribed topics for the subscriber
    std::map<uint16_t, SubscriberTopicInfo>& topics = subscriberInfo->subscriberTopics;

    // find the topic ID in the subscriber's topics
    auto it = topics.find(topicId);
    if (it != topics.end()) {
        return &it->second;
    }

    return nullptr;
}

ClientInfo* MqttSNServer::getSubscriberClientInfo(const inet::L3Address& subscriberAddress, const int& subscriberPort)
{
    // get client information for the subscriber
    ClientInfo* clientInfo = getClientInfo(subscriberAddress, subscriberPort);
    if (clientInfo == nullptr) {
        throw omnetpp::cRuntimeError("Unable to find client information for subscriber (Address: %s, Port: %d).",
                                     subscriberAddress.str().c_str(), subscriberPort);
    }

    return clientInfo;
}

void MqttSNServer::deleteSubscriptionIfExists(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId)
{
    // check for an existing subscription and delete it if found
    std::pair<uint16_t, QoS> subscriptionKey;
    if (findSubscription(subscriberAddress, subscriberPort, topicId, subscriptionKey)) {
        deleteSubscription(subscriberAddress, subscriberPort, subscriptionKey);
    }
}

bool MqttSNServer::findSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId,
                                    std::pair<uint16_t, QoS>& subscriptionKey)
{
    // keys with the same topic ID
    std::set<std::pair<uint16_t, QoS>> keys = getSubscriptionKeysByTopicId(topicId);

    // iterate for each QoS
    for (const auto& key : keys) {
        // check if the subscription exists for the given key
        auto subscriptionIt = subscriptions.find(key);
        if (subscriptionIt != subscriptions.end()) {
            // check if the subscriber is present in the subscription set
            const auto& subscribers = subscriptionIt->second;

            auto subscriberIt = subscribers.find(std::make_pair(subscriberAddress, subscriberPort));
            if (subscriberIt != subscribers.end()) {
                // subscriber found, update the subscription key parameter and return true
                subscriptionKey = key;
                return true;
            }
        }
    }

    // return false if the subscriber has no subscription
    return false;
}

bool MqttSNServer::insertSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort, uint16_t topicId,
                                      TopicIdType topicIdType, QoS qos)
{
    // create key pairs
    std::pair<uint16_t, QoS> subscriptionKey = std::make_pair(topicId, qos);
    std::pair<inet::L3Address, int> subscriberKey = std::make_pair(subscriberAddress, subscriberPort);

    // check if the subscription key already exists in the map
    auto subscriptionIt = subscriptions.find(subscriptionKey);
    if (subscriptionIt != subscriptions.end()) {
        // if the key exists, access the subscribers
        auto& subscribers = subscriptionIt->second;

        // check if the subscriber already exists for the given address and port
        if (subscribers.find(subscriberKey) != subscribers.end()) {
            // if the subscriber already exists, return false
            return false;
        }

        // insert the new subscriber into the existing subscription
        subscribers.insert(subscriberKey);
    }
    else {
        // create a new subscription and insert the new subscriber
        subscriptions[subscriptionKey].insert(subscriberKey);

        // insert the QoS value associated with the topic ID
        topicIdToQoS[topicId].insert(qos);
    }

    // retrieve the subscriber
    SubscriberInfo* subscriberInfo = getSubscriberInfo(subscriberAddress, subscriberPort, true);

    // add a new subscription topic
    SubscriberTopicInfo subscriberTopicInfo;
    subscriberTopicInfo.topicIdType = topicIdType;
    subscriberTopicInfo.isRegistered = true;

    subscriberInfo->subscriberTopics[topicId] = subscriberTopicInfo;

    // return true if the insertion is successful
    return true;
}

bool MqttSNServer::deleteSubscription(const inet::L3Address& subscriberAddress, const int& subscriberPort,
                                      const std::pair<uint16_t, QoS>& subscriptionKey)
{
    // search for the subscription key in the map
    auto subscriptionIt = subscriptions.find(subscriptionKey);
    if (subscriptionIt != subscriptions.end()) {
        // if the key is found, access the subscribers
        auto& subscribers = subscriptionIt->second;

        // check if the subscriber is present
        auto subscriberIt = subscribers.find(std::make_pair(subscriberAddress, subscriberPort));
        if (subscriberIt != subscribers.end()) {
            // delete the subscriber from the map
            subscribers.erase(subscriberIt);

            // check if there are no more subscribers for this subscription key
            if (subscribers.empty()) {
                // remove the subscription key from subscriptions
                subscriptions.erase(subscriptionIt);

                // remove the QoS associated with topic ID if there are no more subscribers
                auto qosSetIt = topicIdToQoS.find(subscriptionKey.first);
                if (qosSetIt != topicIdToQoS.end()) {
                    auto& qosSet = qosSetIt->second;
                    qosSet.erase(subscriptionKey.second);

                    // remove the topic ID if no more QoS are associated
                    if (qosSet.empty()) {
                        topicIdToQoS.erase(qosSetIt);
                    }
                }
            }

            // retrieve the subscriber and delete the subscription topic
            SubscriberInfo* subscriberInfo = getSubscriberInfo(subscriberAddress, subscriberPort, true);
            subscriberInfo->subscriberTopics.erase(subscriptionKey.first);

            // delete operation is successful
            return true;
        }
    }

    // delete operation is failed
    return false;
}

std::set<std::pair<uint16_t, QoS>> MqttSNServer::getSubscriptionKeysByTopicId(uint16_t topicId)
{
    std::set<std::pair<uint16_t, QoS>> keys = {};

    // check if the topic ID key exists in the map
    auto topicIt = topicIdToQoS.find(topicId);
    if (topicIt == topicIdToQoS.end()) {
        // if the topic ID doesn't exist, return an empty set
        return keys;
    }

    const auto& qosSet = topicIt->second;
    // generate complete <topicId, QoS> keys and insert into the set
    for (const auto& qos : qosSet) {
        keys.insert(std::make_pair(topicId, qos));
    }

    return keys;
}

bool MqttSNServer::checkClientsCongestion()
{
    // verify congestion based on the number of clients connected
    return clients.size() >= (unsigned int) par("maximumClients");
}

bool MqttSNServer::checkIDSpaceCongestion(const std::set<uint16_t>& usedIds, bool allowMaxValue)
{
    // check for invalid message ID; ID=0 is invalid
    if (usedIds.find(0) != usedIds.end()) {
        throw omnetpp::cRuntimeError("Invalid message ID=0 detected in the set");
    }

    // check for invalid message ID; ID=UINT16_MAX can be considered invalid
    if (!allowMaxValue && usedIds.find(UINT16_MAX) != usedIds.end()) {
        throw omnetpp::cRuntimeError("Invalid message ID=UINT16_MAX detected in the set");
    }

    uint16_t maxValue = allowMaxValue ? UINT16_MAX : UINT16_MAX - 1;

    // check if the size equals the maximum valid range of IDs
    return (usedIds.size() == maxValue);
}

bool MqttSNServer::checkPublishCongestion(QoS qos, bool retain)
{
    // check congestion for retained messages
    if (retain && checkIDSpaceCongestion(retainMessageIds, false)) {
        return true;
    }

    // check congestion for QoS levels 1 and 2
    if (qos == QoS::QOS_ONE || qos == QoS::QOS_TWO) {
        return (checkIDSpaceCongestion(requestIds) || checkIDSpaceCongestion(messageIds));
    }

    // no congestion detected
    return false;
}

void MqttSNServer::clearPublishersData()
{
    for (auto& publisherPair : publishers) {
        auto& messages = publisherPair.second.messages;

        for (auto it = messages.begin(); it != messages.end();) {
            it = messages.erase(it);
        }
    }
}

void MqttSNServer::clearSubscribersData()
{
    for (auto& subscriberPair : subscribers) {
        auto& topics = subscriberPair.second.subscriberTopics;

        for (auto it = topics.begin(); it != topics.end();) {
            it = topics.erase(it);
        }
    }
}

MqttSNServer::~MqttSNServer()
{
    cancelAndDelete(stateChangeEvent);
    cancelAndDelete(advertiseEvent);
    cancelAndDelete(activeClientsCheckEvent);
    cancelAndDelete(asleepClientsCheckEvent);
    cancelAndDelete(pendingRetainCheckEvent);
    cancelAndDelete(requestsCheckEvent);
    cancelAndDelete(registrationsCheckEvent);
    cancelAndDelete(messagesClearEvent);
}

} /* namespace mqttsn */
