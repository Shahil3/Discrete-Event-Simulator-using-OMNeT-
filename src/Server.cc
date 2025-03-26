#include <omnetpp.h>
#include <sstream>
#include <vector>
#include <algorithm>
#include <string>
#include "messages_m.h"  // Import all 3 auto-generated messages

using namespace omnetpp;
using namespace std;

class Server : public cSimpleModule
{
  private:
    bool isMalicious;
    string serverId;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
};

Define_Module(Server);

void Server::initialize()
{
    serverId = par("serverId").stringValue();
    isMalicious = par("isMalicious").boolValue();
    EV << "Server " << serverId << " initialized. Malicious: " << (isMalicious ? "Yes" : "No") << endl;
}

void Server::handleMessage(cMessage *msg)
{
    auto *task = dynamic_cast<SubtaskMessage *>(msg);
    if (!task) {
        EV << "Received unknown message type\n";
        delete msg;
        return;
    }

    EV << "Server " << serverId << " received subtask from Client " << task->getClientId() << endl;

    vector<int> dataVec;
    int len = task->getDataArraySize();
    for (int i = 0; i < len; ++i) {
        dataVec.push_back(task->getData(i));
    }

    int maxVal = *std::max_element(dataVec.begin(), dataVec.end());

    if (isMalicious) {
        maxVal -= 10;  // Return incorrect result intentionally
    }

    ResultMessage *res = new ResultMessage("ResultMessage");
    res->setClientId(task->getClientId());
    res->setResult(maxVal);

    send(res, "peer$o", msg->getArrivalGate()->getIndex());
    delete msg;
}
