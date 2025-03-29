#include <omnetpp.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include <numeric>
#include "messages_m.h"

using namespace omnetpp;
using namespace std;

// Helper: Ensure that a module's gate vector (named gateName) is at least newSize.
static void ensureGateSize(cModule *mod, const char *gateName, int newSize) {
    int current = mod->gateSize(gateName);
    if (newSize > current)
        mod->setGateSize(gateName, newSize);
}

class Client : public cSimpleModule {
  private:
    // Basic info
    string clientId;
    int numServers, numClients;

    // Tracking connected servers and majority threshold
    int myConnectedServers = 0;
    int myMajority = 0; // (myConnectedServers / 2) + 1

    // For storing partial results from servers (keyed by server id)
    map<int, vector<int>> resultsByServer;
    int receivedResults = 0;

    // Local cumulative scores for servers, keyed by server id
    map<int, int> localServerScores;

    // Mapping from server id to the client's local output gate index for that connection
    map<int, int> serverIdToGate;

    // Gossip data: other clients' scores (clientId -> (server id -> score))
    map<string, map<int, int>> allClientScores;
    unordered_set<string> messageLog; // to avoid duplicate gossip

    // For initial (and optional round 2) arrays
    vector<int> arrayToProcess;
    bool isRound2 = false;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;

    // Helper methods
    void readTopology();
    void findServerConnections();
    void sendInitialSubtasks();
    void sendGossipMessage(const map<int, int>& serverScores);
    map<int, double> calculateAverageScores();
    vector<int> pickTopServers(int k);
};

Define_Module(Client);

void Client::initialize() {
    clientId = par("clientId").stringValue();
    EV << "[Client " << clientId << "] initialized.\n";

    readTopology();          // Read topo.txt and create dynamic connections
    findServerConnections(); // Identify server connections

    if (myConnectedServers == 0) {
        EV << "[Client " << clientId << "] No connected servers. Ending simulation.\n";
        endSimulation();
        return;
    }
    sendInitialSubtasks();   // Send initial subtasks to majority servers
}

void Client::readTopology() {
    ifstream topo("topo.txt");
    if (!topo.is_open()) {
        EV << "❌ [Client " << clientId << "] Cannot open topo.txt\n";
        endSimulation();
        return;
    }

    string line;
    int myIndex = getIndex(); // e.g., client[0] or client[1]

    while (getline(topo, line)) {
        if (line.empty())
            continue;

        if (line.rfind("clients=", 0) == 0)
            numClients = stoi(line.substr(8));
        else if (line.rfind("servers=", 0) == 0)
            numServers = stoi(line.substr(8));
        else if (line.rfind("client", 0) == 0) {
            istringstream ss(line);
            string from, to;
            ss >> from >> to;

            int fromIdx = stoi(from.substr(6));
            if (myIndex != fromIdx)
                continue; // Process only connections where this client is the source

            cModule *net = getParentModule();
            cModule *src = net->getSubmodule("client", fromIdx);
            cModule *dest = net->getSubmodule((to.rfind("client", 0) == 0) ? "client" : "server", stoi(to.substr(6)));

            // Create bidirectional connection
            int gateIdx = src->gateSize("peer");
            ensureGateSize(src, "peer", gateIdx + 1);
            ensureGateSize(dest, "peer", gateIdx + 1);

            src->gate("peer$o", gateIdx)->connectTo(dest->gate("peer$i", gateIdx));
            dest->gate("peer$o", gateIdx)->connectTo(src->gate("peer$i", gateIdx));
        }
    }
    topo.close();
}

void Client::findServerConnections() {
    int countServers = 0;
    // Iterate over our output gates and identify those connected to a Server.
    for (int i = 0; i < gateSize("peer$o"); ++i) {
        cGate *g = gate("peer$o", i);
        if (!g->isConnected())
            continue;

        cModule *otherMod = g->getPathEndGate()->getOwnerModule();
        if (otherMod->getComponentType()->getName() == string("Server")) {
            int serverId = otherMod->getId();
            serverIdToGate[serverId] = i; // map server id to our local gate index
            localServerScores[serverId] = 0; // initialize score
            countServers++;
        }
    }

    myConnectedServers = countServers;
    myMajority = (countServers / 2) + 1;
    EV << "[Client " << clientId << "] Connected to " << myConnectedServers 
       << " servers. Majority: " << myMajority << "\n";
}

void Client::sendInitialSubtasks() {
    arrayToProcess.clear();
    for (int i = 0; i < 12; ++i)
        arrayToProcess.push_back(intuniform(1, 100));

    int toSend = min(myMajority, myConnectedServers);

    // Divide the array evenly among all connected servers.
    int chunkSize = arrayToProcess.size() / myConnectedServers;
    vector<vector<int>> chunks;
    for (int i = 0; i < myConnectedServers; ++i) {
        int start = i * chunkSize;
        int end = (i == myConnectedServers - 1) ? arrayToProcess.size() : start + chunkSize;
        chunks.push_back(vector<int>(arrayToProcess.begin() + start, arrayToProcess.begin() + end));
    }

    int sent = 0;
    // Use the mapping from server id to local gate index.
    for (auto &entry : serverIdToGate) {
        if (sent >= toSend)
            break;
        int gateIdx = entry.second;
        cModule *dest = gate("peer$o", gateIdx)->getPathEndGate()->getOwnerModule();
        if (dest->getComponentType()->getName() != string("Server"))
            continue;

        SubtaskMessage *msg = new SubtaskMessage("Subtask");
        msg->setClientId(getIndex());
        msg->setDataArraySize(chunks[sent].size());
        for (size_t j = 0; j < chunks[sent].size(); ++j)
            msg->setData(j, chunks[sent][j]);

        send(msg, "peer$o", gateIdx);
        sent++;
    }
}

void Client::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage()) {
        if (strcmp(msg->getName(), "StartRound2") == 0) {
            EV << "[Client " << clientId << "] Starting Round 2\n";
            vector<int> topServerIds = pickTopServers(myMajority);
            if (topServerIds.empty()) {
                EV << "[Client " << clientId << "] No top servers selected for Round 2. Aborting round.\n";
                delete msg;
                return;
            }

            vector<int> newArray(12);
            generate(newArray.begin(), newArray.end(), [&](){ return intuniform(1, 100); });

            int chunkSize = newArray.size() / topServerIds.size();
            for (size_t i = 0; i < topServerIds.size(); ++i) {
                int start = i * chunkSize;
                int end = (i == topServerIds.size() - 1) ? newArray.size() : start + chunkSize;
                vector<int> chunk(newArray.begin() + start, newArray.begin() + end);

                SubtaskMessage *subtask = new SubtaskMessage("Subtask");
                subtask->setDataArraySize(chunk.size());
                for (size_t j = 0; j < chunk.size(); ++j)
                    subtask->setData(j, chunk[j]);

                // Look up the local gate index for this server id.
                int localGate = serverIdToGate[topServerIds[i]];
                send(subtask, "peer$o", localGate);
            }
            delete msg;
        }
        return;
    }

    if (auto *resultMsg = dynamic_cast<ResultMessage *>(msg)) {
        // Extract the server's id from the sender module.
        cGate *arrivalGate = msg->getArrivalGate();
        cGate *senderGate = arrivalGate->getPreviousGate();
        int serverId = senderGate->getOwnerModule()->getId();
        resultsByServer[serverId].push_back(resultMsg->getResult());
        receivedResults++;

        if (receivedResults >= myMajority && !isRound2) {
            int expectedSum = accumulate(arrayToProcess.begin(), arrayToProcess.end(), 0);
            for (auto &[srvId, results] : resultsByServer) {
                for (int res : results) {
                    localServerScores[srvId] += (res == expectedSum) ? 1 : -1;
                }
            }

            sendGossipMessage(localServerScores);
            scheduleAt(simTime() + 0.1, new cMessage("StartRound2"));
            isRound2 = true;
        }
        delete msg;
        return;
    }

    if (auto *gossip = dynamic_cast<GossipMessage *>(msg)) {
        string content = gossip->getContent();
        string hashValue = to_string(hash<string>{}(content));

        if (messageLog.find(hashValue) == messageLog.end()) {
            messageLog.insert(hashValue);

            // Parse the gossip message: Format: timestamp:senderId:s<serverId>=<score>#...
            istringstream ss(content);
            string ts, sender, scores;
            getline(ss, ts, ':');
            getline(ss, sender, ':');
            getline(ss, scores, ':');

            map<int, int> parsedScores;
            stringstream scoreStream(scores);
            string entry;
            while (getline(scoreStream, entry, '#')) {
                if (entry.empty())
                    continue;
                size_t eqPos = entry.find('=');
                if (eqPos != string::npos) {
                    int serverId = stoi(entry.substr(1, eqPos - 1));
                    int score = stoi(entry.substr(eqPos + 1));
                    parsedScores[serverId] = score;
                }
            }
            allClientScores[sender] = parsedScores;

            // Forward gossip to all connected output gates except the arrival gate.
            int arrivalGate = gossip->getArrivalGate()->getIndex();
            for (int i = 0; i < gateSize("peer$o"); ++i) {
                cGate *g = gate("peer$o", i);
                if (g->isConnected() && i != arrivalGate) {
                    send(gossip->dup(), "peer$o", i);
                }
            }
        }
        delete msg;
    }
}

/*
   sendGossipMessage() creates a gossip message (using server ids, not local gate indices)
   and sends it to all connected server gates. Also, we store our own scores into allClientScores,
   so that our averages only include servers we are connected to.
*/
void Client::sendGossipMessage(const map<int, int>& serverScores) {
    stringstream content;
    content << time(nullptr) << ":" << clientId << ":";
    for (auto &[serverId, score] : serverScores)
        content << "s" << serverId << "=" << score << "#";

    GossipMessage *gm = new GossipMessage("Gossip");
    gm->setContent(content.str().c_str());
    string hashValue = to_string(hash<string>{}(content.str()));
    messageLog.insert(hashValue);

    // Include our own scores in gossip.
    allClientScores[clientId] = serverScores;

    // Send gossip only on gates connected to clients.
    for (int i = 0; i < gateSize("peer$o"); ++i) {
        cGate *g = gate("peer$o", i);
        if (g->isConnected()) {
            cModule *dest = g->getPathEndGate()->getOwnerModule();
            if (dest->getComponentType()->getName() == string("Client")) {
                send(gm->dup(), "peer$o", i);
            }
        }
    }
    delete gm;
}

/*
   calculateAverageScores() computes the average score per server (keyed by server id)
   using gossip messages received from other clients.
   --- FIX: Only include server ids that are in our local mapping.
*/
map<int, double> Client::calculateAverageScores() {
    map<int, double> avg;
    map<int, pair<int, int>> totals; // pair: (sum, count)

    for (auto &[client, scores] : allClientScores) {
        for (auto &[serverId, score] : scores) {
            // Only consider server ids that we are connected to.
            if (serverIdToGate.find(serverId) != serverIdToGate.end()) {
                totals[serverId].first += score;
                totals[serverId].second++;
            }
        }
    }

    for (auto &[serverId, data] : totals)
        avg[serverId] = static_cast<double>(data.first) / data.second;

    return avg;
}

/*
   pickTopServers() returns the top k server ids (by average score)
   from the computed average scores.
*/
vector<int> Client::pickTopServers(int k) {
    auto avgScores = calculateAverageScores();
    vector<pair<int, double>> sorted(avgScores.begin(), avgScores.end());
    sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) { return a.second > b.second; });

    vector<int> top;
    for (int i = 0; i < min(k, static_cast<int>(sorted.size())); ++i)
        top.push_back(sorted[i].first);

    return top;
}