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

    // For storing partial results from servers (keyed by custom serverId)
    map<string, vector<int>> resultsByServer;
    int receivedResults = 0;

    // Local cumulative scores for servers, keyed by custom serverId
    map<string, int> localServerScores;

    // Mapping from custom serverId to the client's local output gate index for that connection
    map<string, int> serverIdToGate;

    // Gossip data: other clients' scores (clientId -> (custom serverId -> score))
    map<string, map<string, int>> allClientScores;
    unordered_set<string> messageLog; // to avoid duplicate gossip

    // For initial (and optional round 2) arrays
    vector<int> arrayToProcess;
    bool isRound2 = false;

    // Mapping from custom serverId to the expected maximum value for the chunk sent.
    map<string, int> expectedMaxByServer;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;

    // Helper methods
    void readTopology();
    void findServerConnections();
    void sendInitialSubtasks();
    void sendGossipMessage(const map<string, int>& serverScores);
    map<string, double> calculateAverageScores();
    vector<string> pickTopServers(int k);
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
    EV << "[Client " << clientId << "] Reading topology file...\n";

    while (getline(topo, line)) {
        if (line.empty())
            continue;

        if (line.rfind("clients=", 0) == 0) {
            numClients = stoi(line.substr(8));
            EV << "[Client " << clientId << "] Total clients: " << numClients << "\n";
        }
        else if (line.rfind("servers=", 0) == 0) {
            numServers = stoi(line.substr(8));
            EV << "[Client " << clientId << "] Total servers: " << numServers << "\n";
        }
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

            EV << "[Client " << clientId << "] Created connection from client[" << fromIdx << "] to " 
               << ((to.rfind("client", 0) == 0) ? "client" : "server") << "[" << to.substr(6) << "] at gate index " << gateIdx << "\n";
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
            // Use the custom serverId defined as a string parameter
            string serverIdParam = otherMod->par("serverId").stringValue();
            serverIdToGate[serverIdParam] = i; // map custom serverId to our local gate index
            localServerScores[serverIdParam] = 0; // initialize score
            countServers++;
            EV << "[Client " << clientId << "] Found connection to Server (serverId=" 
               << serverIdParam << ") at local gate index " << i << "\n";
        }
    }

    myConnectedServers = countServers;
    myMajority = (countServers / 2) + 1;
    EV << "[Client " << clientId << "] Connected to " << myConnectedServers 
       << " servers. Majority threshold: " << myMajority << "\n";
}

void Client::sendInitialSubtasks() {
    arrayToProcess.clear();
    for (int i = 0; i < 12; ++i) {
        int randomVal = intuniform(1, 100);
        arrayToProcess.push_back(randomVal);
    }
    EV << "[Client " << clientId << "] Generated array to process: ";
    for (int val : arrayToProcess)
        EV << val << " ";
    EV << "\n";

    int toSend = min(myMajority, myConnectedServers);

    // Divide the array evenly among all connected servers.
    int chunkSize = arrayToProcess.size() / myConnectedServers;
    vector<vector<int>> chunks;
    for (int i = 0; i < myConnectedServers; ++i) {
        int start = i * chunkSize;
        int end = (i == myConnectedServers - 1) ? arrayToProcess.size() : start + chunkSize;
        chunks.push_back(vector<int>(arrayToProcess.begin() + start, arrayToProcess.begin() + end));
    }

    // Print out the chunks that will be sent to each server.
    for (size_t i = 0; i < chunks.size(); ++i) {
        EV << "[Client " << clientId << "] Chunk " << i << ": ";
        for (int num : chunks[i])
            EV << num << " ";
        EV << "\n";
    }

    int sent = 0;
    // Use the mapping from custom serverId to local gate index.
    for (auto &entry : serverIdToGate) {
        if (sent >= toSend)
            break;
        int gateIdx = entry.second;
        cModule *dest = gate("peer$o", gateIdx)->getPathEndGate()->getOwnerModule();
        if (dest->getComponentType()->getName() != string("Server"))
            continue;

        // Use the custom serverId defined as a string
        string serverIdParam = dest->par("serverId").stringValue();
        // Compute expected maximum value for this chunk.
        int expectedMax = *max_element(chunks[sent].begin(), chunks[sent].end());
        expectedMaxByServer[serverIdParam] = expectedMax;

        SubtaskMessage *msg = new SubtaskMessage("Subtask");
        msg->setClientId(getIndex());
        msg->setDataArraySize(chunks[sent].size());
        for (size_t j = 0; j < chunks[sent].size(); ++j)
            msg->setData(j, chunks[sent][j]);

        EV << "[Client " << clientId << "] Sending Subtask (chunk " << sent << ") to Server (serverId=" 
           << serverIdParam << ") via gate index " << gateIdx << ". Expected max: " << expectedMax << "\n";
        send(msg, "peer$o", gateIdx);
        sent++;
    }
}

void Client::handleMessage(cMessage *msg) {
    if (msg->isSelfMessage()) {
        if (strcmp(msg->getName(), "StartRound2") == 0) {
            EV << "[Client " << clientId << "] Starting Round 2\n";
            vector<string> topServerIds = pickTopServers(myMajority);
            if (topServerIds.empty()) {
                EV << "[Client " << clientId << "] No top servers selected for Round 2. Aborting round.\n";
                delete msg;
                return;
            }

            EV << "[Client " << clientId << "] Top servers for Round 2: ";
            for (const auto &srv : topServerIds)
                EV << srv << " ";
            EV << "\n";

            vector<int> newArray(12);
            generate(newArray.begin(), newArray.end(), [&](){ return intuniform(1, 100); });

            EV << "[Client " << clientId << "] Generated new array for Round 2: ";
            for (int num : newArray)
                EV << num << " ";
            EV << "\n";

            int chunkSize = newArray.size() / topServerIds.size();
            for (size_t i = 0; i < topServerIds.size(); ++i) {
                int start = i * chunkSize;
                int end = (i == topServerIds.size() - 1) ? newArray.size() : start + chunkSize;
                vector<int> chunk(newArray.begin() + start, newArray.begin() + end);

                SubtaskMessage *subtask = new SubtaskMessage("Subtask");
                subtask->setDataArraySize(chunk.size());
                for (size_t j = 0; j < chunk.size(); ++j)
                    subtask->setData(j, chunk[j]);

                int localGate = serverIdToGate[topServerIds[i]];
                EV << "[Client " << clientId << "] Sending Round 2 subtask (chunk " << i 
                   << ") to Server (serverId=" << topServerIds[i] << ") via gate index " << localGate << "\n";
                send(subtask, "peer$o", localGate);
            }
            delete msg;
        }
        return;
    }

    if (auto *resultMsg = dynamic_cast<ResultMessage *>(msg)) {
        // Extract the custom serverId from the sender module's parameter.
        cGate *arrivalGate = msg->getArrivalGate();
        cGate *senderGate = arrivalGate->getPreviousGate();
        string serverIdParam = senderGate->getOwnerModule()->par("serverId").stringValue();
        resultsByServer[serverIdParam].push_back(resultMsg->getResult());
        receivedResults++;
        EV << "[Client " << clientId << "] Received result " << resultMsg->getResult() 
           << " from Server (serverId=" << serverIdParam << "). Total received: " << receivedResults << "\n";

        if (receivedResults >= myMajority && !isRound2) {
            // For each server, compare the reported result to the expected maximum for its chunk.
            for (auto &entry : resultsByServer) {
                const string &srvId = entry.first;
                int expectedMax = expectedMaxByServer[srvId];
                EV << "[Client " << clientId << "] Expected max for Server (serverId=" << srvId << "): " << expectedMax << "\n";
                for (int res : entry.second) {
                    if (res == expectedMax) {
                        localServerScores[srvId] += 1;
                        EV << "[Client " << clientId << "] Server (serverId=" << srvId 
                           << ") reported correct max result. Increasing score.\n";
                    }
                    else {
                        localServerScores[srvId] -= 1;
                        EV << "[Client " << clientId << "] Server (serverId=" << srvId 
                           << ") reported incorrect max result (" << res << "). Decreasing score.\n";
                    }
                }
            }

            sendGossipMessage(localServerScores);
            EV << "[Client " << clientId << "] Scheduling Round 2 in 0.1 seconds.\n";
            scheduleAt(simTime() + 0.1, new cMessage("StartRound2"));
            isRound2 = true;
        }
        delete msg;
        return;
    }

    if (auto *gossip = dynamic_cast<GossipMessage *>(msg)) {
        string content = gossip->getContent();
        string hashValue = to_string(hash<string>{}(content));
        EV << "[Client " << clientId << "] Received Gossip message: " << content << "\n";

        if (messageLog.find(hashValue) == messageLog.end()) {
            EV << "[Client " << clientId << "] Processing new Gossip message.\n";
            messageLog.insert(hashValue);

            // Parse the gossip message: Format: timestamp:clientId:s<serverId>=<score>#...
            istringstream ss(content);
            string ts, sender, scores;
            getline(ss, ts, ':');
            getline(ss, sender, ':');
            getline(ss, scores, ':');

            map<string, int> parsedScores;
            stringstream scoreStream(scores);
            string entry;
            while (getline(scoreStream, entry, '#')) {
                if (entry.empty())
                    continue;
                size_t eqPos = entry.find('=');
                if (eqPos != string::npos) {
                    // Extract the custom serverId as a string.
                    string serverIdParam = entry.substr(1, eqPos - 1);
                    int score = stoi(entry.substr(eqPos + 1));
                    parsedScores[serverIdParam] = score;
                }
            }
            allClientScores[sender] = parsedScores;
            EV << "[Client " << clientId << "] Parsed scores from " << sender << ": ";
            for (auto &p : parsedScores)
                EV << "s" << p.first << "=" << p.second << " ";
            EV << "\n";

            int arrivalGate = gossip->getArrivalGate()->getIndex();
            // Forward gossip only to connected clients (skip the arrival gate)
            for (int i = 0; i < gateSize("peer$o"); ++i) {
                cGate *g = gate("peer$o", i);
                if (g->isConnected() && i != arrivalGate) {
                    cModule *dest = g->getPathEndGate()->getOwnerModule();
                    if (dest->getComponentType()->getName() == string("Client")) {
                        EV << "[Client " << clientId << "] Forwarding gossip to client via gate index " << i << "\n";
                        send(gossip->dup(), "peer$o", i);
                    }
                }
            }
        }
        else {
            EV << "[Client " << clientId << "] Duplicate gossip received. Ignoring.\n";
        }
        delete msg;
    }
}

/*
   sendGossipMessage() creates a gossip message (using custom serverIds)
   and sends it to all connected client gates. Also, we store our own scores into allClientScores.
*/
void Client::sendGossipMessage(const map<string, int>& serverScores) {
    stringstream content;
    content << time(nullptr) << ":" << clientId << ":";
    for (auto &entry : serverScores)
        content << "s" << entry.first << "=" << entry.second << "#";

    GossipMessage *gm = new GossipMessage("Gossip");
    gm->setContent(content.str().c_str());
    string hashValue = to_string(hash<string>{}(content.str()));
    messageLog.insert(hashValue);

    // Include our own scores in gossip.
    allClientScores[clientId] = serverScores;

    EV << "[Client " << clientId << "] Sending Gossip message: " << content.str() << "\n";
    // Send gossip only on gates connected to clients.
    for (int i = 0; i < gateSize("peer$o"); ++i) {
        cGate *g = gate("peer$o", i);
        if (g->isConnected()) {
            cModule *dest = g->getPathEndGate()->getOwnerModule();
            if (dest->getComponentType()->getName() == string("Client")) {
                send(gm->dup(), "peer$o", i);
                // Note: For clients, you might not have a "serverId" parameter.
                EV << "[Client " << clientId << "] Gossip forwarded to Client (id=" 
                   << dest->getId() << ") via gate index " << i << "\n";
            }
        }
    }
    delete gm;
}

map<string, double> Client::calculateAverageScores() {
    map<string, double> avg;
    map<string, pair<int, int>> totals; // pair: (sum, count)

    for (auto &clientScores : allClientScores) {
        for (auto &entry : clientScores.second) {
            const string &serverId = entry.first;
            // Only consider custom serverIds that we are connected to.
            if (serverIdToGate.find(serverId) != serverIdToGate.end()) {
                totals[serverId].first += entry.second;
                totals[serverId].second++;
            }
        }
    }

    for (auto &entry : totals)
        avg[entry.first] = static_cast<double>(entry.second.first) / entry.second.second;

    return avg;
}

vector<string> Client::pickTopServers(int k) {
    auto avgScores = calculateAverageScores();
    vector<pair<string, double>> sorted(avgScores.begin(), avgScores.end());
    sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) { return a.second > b.second; });

    vector<string> top;
    for (int i = 0; i < min(k, static_cast<int>(sorted.size())); ++i)
        top.push_back(sorted[i].first);

    return top;
}