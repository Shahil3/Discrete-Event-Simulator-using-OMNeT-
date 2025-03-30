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
#include "messages_m.h"  // Ensure SubtaskMessage, ResultMessage, and GossipMessage are defined

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

    // For storing results per subtask:
    // For each subtask (key: subtask id) we store a mapping (serverId -> reported result)
    map<int, map<string, int>> resultsBySubtask;
    // Count how many subtasks have been fully processed (i.e. received results from all servers)
    int completedSubtasks = 0;
    // Total number of subtasks for the round.
    int numSubtasks = 5;

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

    // Mapping from subtask id to the expected maximum value for that subtask (for logging)
    map<int, int> expectedMaxBySubtask;
    // To track which subtasks have been processed already so we don't update scores twice.
    unordered_set<int> processedSubtasks;

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
    sendInitialSubtasks();   // Send initial subtasks to connected servers
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
            // Determine destination module type based on prefix in "to"
            string destModuleName = (to.rfind("client", 0) == 0) ? "client" : "server";
            cModule *dest = net->getSubmodule(destModuleName.c_str(), stoi(to.substr(6)));

            // --- Forward connection: from src to dest ---
            int srcOutIdx = src->gateSize("peer");
            int destInIdx = dest->gateSize("peer");
            ensureGateSize(src, "peer", srcOutIdx + 1);
            ensureGateSize(dest, "peer", destInIdx + 1);
            src->gate("peer$o", srcOutIdx)->connectTo(dest->gate("peer$i", destInIdx));
            EV << "[Client " << clientId << "] Created connection from client[" << fromIdx << "] to " 
               << destModuleName << "[" << to.substr(6) 
               << "] at src gate index " << srcOutIdx << " and dest gate index " << destInIdx << "\n";

            // --- Reverse connection: from dest to src ---
            int destOutIdx = dest->gateSize("peer");
            int srcInIdx = src->gateSize("peer");
            ensureGateSize(dest, "peer", destOutIdx + 1);
            ensureGateSize(src, "peer", srcInIdx + 1);
            dest->gate("peer$o", destOutIdx)->connectTo(src->gate("peer$i", srcInIdx));
            EV << "[Client " << clientId << "] Created reverse connection from " 
               << destModuleName << "[" << to.substr(6) << "] to client[" << fromIdx 
               << "] at dest gate index " << destOutIdx << " and src gate index " << srcInIdx << "\n";
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
    for (int i = 0; i < 10; ++i) {
        int randomVal = intuniform(1, 100);
        arrayToProcess.push_back(randomVal);
    }
    EV << "[Client " << clientId << "] Generated array to process: ";
    for (int val : arrayToProcess)
        EV << val << " ";
    EV << "\n";

    // Divide the array into a fixed number of subtasks (numSubtasks = 5)
    vector<vector<int>> chunks;
    int chunkSize = arrayToProcess.size() / numSubtasks;
    for (int i = 0; i < numSubtasks; ++i) {
        int start = i * chunkSize;
        int end = (i == numSubtasks - 1) ? arrayToProcess.size() : start + chunkSize;
        chunks.push_back(vector<int>(arrayToProcess.begin() + start, arrayToProcess.begin() + end));

        // For logging: compute and record the expected maximum for this subtask
        int expectedMax = *max_element(chunks[i].begin(), chunks[i].end());
        expectedMaxBySubtask[i] = expectedMax;
        EV << "[Client " << clientId << "] Subtask " << i << " data: ";
        for (int num : chunks[i])
            EV << num << " ";
        EV << " | Expected max (for logging): " << expectedMax << "\n";
    }

    // For each subtask, send the same message to every connected server.
    for (size_t subtaskId = 0; subtaskId < chunks.size(); ++subtaskId) {
        for (auto &entry : serverIdToGate) {
            int gateIdx = entry.second;
            cModule *dest = gate("peer$o", gateIdx)->getPathEndGate()->getOwnerModule();
            if (dest->getComponentType()->getName() != string("Server"))
                continue;

            string serverIdParam = dest->par("serverId").stringValue();
            SubtaskMessage *msg = new SubtaskMessage("Subtask");
            msg->setClientId(getIndex());
            msg->setSubtaskId(subtaskId);
            msg->setDataArraySize(chunks[subtaskId].size());
            for (size_t j = 0; j < chunks[subtaskId].size(); ++j)
                msg->setData(j, chunks[subtaskId][j]);

            EV << "[Client " << clientId << "] Sending Subtask " << subtaskId 
               << " to Server (serverId=" << serverIdParam << ") via gate index " << gateIdx 
               << ". Expected max (for logging): " << expectedMaxBySubtask[subtaskId] << "\n";
            send(msg, "peer$o", gateIdx);
        }
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

            // Generate a new array for Round 2.
            vector<int> newArray(12);
            generate(newArray.begin(), newArray.end(), [&](){ return intuniform(1, 100); });

            EV << "[Client " << clientId << "] Generated new array for Round 2: ";
            for (int num : newArray)
                EV << num << " ";
            EV << "\n";

            // For Round 2, send the entire array to each top server (no chunking).
            for (size_t i = 0; i < topServerIds.size(); ++i) {
                SubtaskMessage *subtask = new SubtaskMessage("Subtask");
                subtask->setClientId(getIndex());
                subtask->setSubtaskId(i + myConnectedServers + 100);
                subtask->setDataArraySize(newArray.size());
                for (size_t j = 0; j < newArray.size(); ++j)
                    subtask->setData(j, newArray[j]);

                int localGate = serverIdToGate[topServerIds[i]];
                EV << "[Client " << clientId << "] Sending Round 2 subtask (full array) to Server (serverId="
                   << topServerIds[i] << ") via gate index " << localGate << "\n";
                send(subtask, "peer$o", localGate);
            }
            delete msg;
            return;
        }
    }

    if (auto *resultMsg = dynamic_cast<ResultMessage *>(msg)) {
        int subtaskId = resultMsg->getSubtaskId();
        if (processedSubtasks.find(subtaskId) != processedSubtasks.end()) {
            EV << "[Client " << clientId << "] Subtask " << subtaskId << " already processed. Ignoring duplicate result.\n";
            delete msg;
            return;
        }

        cGate *arrivalGate = msg->getArrivalGate();
        cGate *senderGate = arrivalGate->getPreviousGate();
        string serverIdParam = senderGate->getOwnerModule()->par("serverId").stringValue();

        resultsBySubtask[subtaskId][serverIdParam] = resultMsg->getResult();
        EV << "[Client " << clientId << "] Received result for Subtask " << subtaskId
           << " from Server (serverId=" << serverIdParam << "): " << resultMsg->getResult() << "\n";

        EV << "[Client " << clientId << "] Current results for Subtask " << subtaskId << ": ";
        for (auto &entry : resultsBySubtask[subtaskId]) {
            EV << "[server " << entry.first << " -> " << entry.second << "] ";
        }
        EV << "\n";

        if (resultsBySubtask[subtaskId].size() == (size_t)myConnectedServers) {
            EV << "[Client " << clientId << "] All results received for Subtask " << subtaskId << ". Processing consensus...\n";
            map<int, int> frequency;
            for (auto &p : resultsBySubtask[subtaskId])
                frequency[p.second]++;

            int consensusResult = -1;
            int maxCount = 0;
            for (auto &entry : frequency) {
                if (entry.second > maxCount) {
                    maxCount = entry.second;
                    consensusResult = entry.first;
                }
            }
            EV << "[Client " << clientId << "] For Subtask " << subtaskId 
               << ", consensus result: " << consensusResult 
               << " (reported by " << maxCount << " servers).\n";

            for (auto &p : resultsBySubtask[subtaskId]) {
                if (p.second == consensusResult) {
                    localServerScores[p.first] += 1;
                    EV << "[Client " << clientId << "] Server (serverId=" << p.first 
                       << ") reported the consensus result. Increasing score. New score: " << localServerScores[p.first] << "\n";
                } else {
                    localServerScores[p.first] -= 1;
                    EV << "[Client " << clientId << "] Server (serverId=" << p.first 
                       << ") did not report the consensus result (" << p.second 
                       << "). Decreasing score. New score: " << localServerScores[p.first] << "\n";
                }
            }
            processedSubtasks.insert(subtaskId);
            completedSubtasks++;
            EV << "[Client " << clientId << "] Completed subtasks: " << completedSubtasks << "/" << numSubtasks << "\n";

            if (completedSubtasks >= numSubtasks && !isRound2) {
                EV << "[Client " << clientId << "] All subtasks processed. Sending gossip and scheduling Round 2 in 0.1 seconds.\n";
                sendGossipMessage(localServerScores);
                scheduleAt(simTime() + 0.1, new cMessage("StartRound2"));
                isRound2 = true;
            }
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

            for (auto &entry : serverIdToGate) {
                const string &serverId = entry.first;
                if (parsedScores.find(serverId) != parsedScores.end()) {
                    int receivedScore = parsedScores[serverId];
                    int currentLocal = localServerScores[serverId];
                    int newScore = (currentLocal + receivedScore) / 2;
                    localServerScores[serverId] = newScore;
                    EV << "[Client " << clientId << "] Updated local score for server (serverId=" 
                       << serverId << ") to " << newScore << " (average of " << currentLocal 
                       << " and " << receivedScore << ")\n";
                }
            }

            int arrivalGate = gossip->getArrivalGate()->getIndex();
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

void Client::sendGossipMessage(const map<string, int>& serverScores) {
    stringstream content;
    content << time(nullptr) << ":" << clientId << ":";
    for (auto &entry : serverScores)
        content << "s" << entry.first << "=" << entry.second << "#";

    GossipMessage *gm = new GossipMessage("Gossip");
    gm->setContent(content.str().c_str());
    string hashValue = to_string(hash<string>{}(content.str()));
    messageLog.insert(hashValue);

    allClientScores[clientId] = serverScores;

    EV << "[Client " << clientId << "] Sending Gossip message: " << content.str() << "\n";
    for (int i = 0; i < gateSize("peer$o"); ++i) {
        cGate *g = gate("peer$o", i);
        if (g->isConnected()) {
            cModule *dest = g->getPathEndGate()->getOwnerModule();
            if (dest->getComponentType()->getName() == string("Client")) {
                send(gm->dup(), "peer$o", i);
                EV << "[Client " << clientId << "] Gossip forwarded to Client (id=" 
                   << dest->getId() << ") via gate index " << i << "\n";
            }
        }
    }
    delete gm;
}

map<string, double> Client::calculateAverageScores() {
    map<string, double> avg;
    map<string, pair<int, int>> totals;

    for (auto &clientScores : allClientScores) {
        for (auto &entry : clientScores.second) {
            const string &serverId = entry.first;
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

    EV << "[Client " << clientId << "] Average scores: ";
    for (auto &p : sorted)
        EV << "s" << p.first << "=" << p.second << " ";
    EV << "\n";
    return top;
}