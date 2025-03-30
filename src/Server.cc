#include <omnetpp.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <string>
#include "messages_m.h"  // Import all auto-generated messages

using namespace omnetpp;
using namespace std;

class Server : public cSimpleModule {
  private:
    bool isMalicious;
    string serverId;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

Define_Module(Server);

void Server::initialize() {
    serverId = par("serverId").stringValue();
    isMalicious = par("isMalicious").boolValue();
    EV << "Server " << serverId << " initialized. Malicious: " << (isMalicious ? "Yes" : "No") << endl;
}

void Server::handleMessage(cMessage *msg) {
    // Process only SubtaskMessage messages.
    auto *task = dynamic_cast<SubtaskMessage *>(msg);
    if (!task) {
        EV << "Received unknown message type\n";
        delete msg;
        return;
    }

    EV << "Server " << serverId << " received subtask (ID: " << task->getSubtaskId() 
       << ") from Client " << task->getClientId() << endl;

    // Process the subtask data.
    vector<int> dataVec;
    int len = task->getDataArraySize();
    for (int i = 0; i < len; ++i) {
        dataVec.push_back(task->getData(i));
    }
    int maxVal = *max_element(dataVec.begin(), dataVec.end());
    if (isMalicious) {
        maxVal -= 10;  // Return incorrect result intentionally
    }

    // Create the result message.
    ResultMessage *res = new ResultMessage("ResultMessage");
    res->setClientId(task->getClientId());
    res->setResult(maxVal);
    res->setSubtaskId(task->getSubtaskId());

    // Find the correct reverse connection gate:
    // (We want the gate on our "peer$o" array that connects to the client that sent the subtask.)
    cGate *arrivalGate = msg->getArrivalGate();      // This is our peer$i gate where the subtask arrived.
    cGate *senderGate  = arrivalGate->getPreviousGate();  // This is the client's peer$o gate.
    cModule *clientModule = senderGate->getOwnerModule();

    int reverseGateIndex = -1;
    int n = gateSize("peer$o");
    for (int i = 0; i < n; ++i) {
        cGate *outGate = gate("peer$o", i);
        if (outGate->isConnected() && outGate->getPathEndGate()->getOwnerModule() == clientModule) {
            reverseGateIndex = i;
            break;
        }
    }
    if (reverseGateIndex == -1) {
        EV << "Error: No reverse connection found to client " << clientModule->getFullPath() << "\n";
        delete res;
        delete msg;
        return;
    }

    EV << "Server " << serverId << " sending result for Subtask " 
       << task->getSubtaskId() << ": " << maxVal << " via gate peer$o[" << reverseGateIndex << "]" << endl;
    send(res, "peer$o", reverseGateIndex);
    delete msg;
}