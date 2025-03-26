#include <omnetpp.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <ctime>
#include "messages_m.h"

using namespace omnetpp;
using namespace std;

class Client : public cSimpleModule
{
  private:
    // Basic info
    string clientId;
    int numServers;  // total servers in network (from topo.txt)
    int numClients;  // total clients in network (from topo.txt)

    // We'll track how many *actual server connections* we have
    int myConnectedServers = 0;
    int myMajority = 0; // e.g. myConnectedServers/2 + 1

    // For storing partial results from servers
    map<int, vector<int>> resultsByGate;
    int receivedResults = 0;

    // For storing gossip
    unordered_set<string> messageLog;                   // store gossip hashes
    map<string, map<int, int>> allClientScores;         // senderClientId -> (gate -> score)

    // For initial (and optional round 2) arrays
    vector<int> arrayToProcess;
    bool isRound2 = false;

  protected:
    // cSimpleModule overrides
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;

    // Helper methods
    void readTopology();
    void findServerConnections();     // dynamic check of which gates connect to servers
    void sendInitialSubtasks();
    void sendGossipMessage(map<int, int> serverScores);
    map<int, double> calculateAverageScores();
    vector<int> pickTopServers(int k);
};

Define_Module(Client);

//
// Called once at simulation start
//
void Client::initialize()
{
    clientId = par("clientId").stringValue();
    EV << "[Client " << clientId << "] initialized.\n";

    readTopology();          // read topo.txt, set numServers/numClients
    findServerConnections(); // figure out which gates actually connect to servers
    sendInitialSubtasks();   // send tasks to a majority of actual connected servers
}

//
// Reads topo.txt to discover how many total clients/servers exist.
// Also sets up connections (but only from client[0]) if you wish.
//
void Client::readTopology()
{
    ifstream topo("topo.txt");
    if (!topo.is_open()) {
        EV << "❌ [Client " << clientId << "] Cannot open topo.txt\n";
        endSimulation();
        return;
    }

    string line;
    int myIndex = getIndex(); // e.g. client[0] or client[1]

    while (getline(topo, line)) {
        if (line.empty()) continue;

        // e.g. "clients=2" -> store in numClients
        if (line.rfind("clients=", 0) == 0) {
            numClients = stoi(line.substr(8));
        }
        // e.g. "servers=4" -> store in numServers
        else if (line.rfind("servers=", 0) == 0) {
            numServers = stoi(line.substr(8));
        }
        // e.g. "client0 server1"
        else if (line.rfind("client", 0) == 0) {
            // We do dynamic connection only from client0 to keep code simpler
            if (myIndex == 0) {
                istringstream ss(line);
                string from, to;
                ss >> from >> to; // e.g. "client0" and "server1"

                int fromIdx = stoi(from.substr(6));  // after 'client'
                int toIdx;
                string modType;

                if (to.rfind("client", 0) == 0) {
                    modType = "client";
                    toIdx = stoi(to.substr(6));
                } else {
                    modType = "server";
                    toIdx = stoi(to.substr(6));
                }

                cModule *net = getParentModule();
                cModule *src  = net->getSubmodule("client", fromIdx);
                cModule *dest = net->getSubmodule(modType.c_str(), toIdx);

                int srcGate = src->gateSize("peer$o");
                src->setGateSize("peer", srcGate + 1);
                dest->setGateSize("peer", srcGate + 1);

                src->gate("peer$o", srcGate)->connectTo(dest->gate("peer$i", srcGate));
                dest->gate("peer$o", srcGate)->connectTo(src->gate("peer$i", srcGate));

                EV << "[Client0 Setup] Connected " << from << " <--> " << to << endl;
            }
        }
    }
    topo.close();

    EV << "[Client " << clientId << "] Read topo: numClients=" << numClients
       << ", numServers=" << numServers << endl;
}

//
// Finds which of this client's gates actually connect to a 'Server' module
// and sets myConnectedServers + myMajority accordingly.
//
void Client::findServerConnections()
{
    int totalGates = gateSize("peer$o");
    int countServers = 0;

    for (int i = 0; i < totalGates; ++i) {
        // Check if this gate is connected
        cGate *outGate = gate("peer$o", i);
        if (!outGate->isConnected()) continue;

        // Follow the path to the other side
        cGate *otherGate = outGate->getPathEndGate();
        if (!otherGate) continue;

        cModule *otherMod = otherGate->getOwnerModule();

        // Check if the other module is actually a 'Server'
        // Compare the C++ class name or some param
        const char *typeName = otherMod->getComponentType()->getName();
        if (strcmp(typeName, "Server") == 0) {
            countServers++;
        }
    }

    myConnectedServers = countServers;
    myMajority = myConnectedServers / 2 + 1;

    EV << "[Client " << clientId << "] Actually connected to " 
       << myConnectedServers << " servers. => majority=" << myMajority << endl;
}

//
// Sends initial subtasks to 'myMajority' servers (the first N server gates).
// Splits an array of 12 random ints into the same # of chunks as 'myConnectedServers'.
//
void Client::sendInitialSubtasks()
{
    // 1) Create a random array of size 12
    arrayToProcess.clear();
    for (int i = 0; i < 12; ++i) {
        arrayToProcess.push_back(intuniform(1, 100));
    }

    // Print the entire array
    EV << "[Client " << clientId << "] Generated initial array (12 numbers): ";
    for (int val : arrayToProcess) {
        EV << val << " ";
    }
    EV << endl;

    // 2) Split into myConnectedServers chunks
    if (myConnectedServers == 0) {
        EV << "[Client " << clientId << "] No servers connected, skipping.\n";
        return;
    }

    int chunkSize = arrayToProcess.size() / myConnectedServers; 
    vector<vector<int>> chunks;
    for (int i = 0; i < myConnectedServers; ++i) {
        int startIdx = i * chunkSize;
        int endIdx   = (i + 1) * chunkSize;
        if (i == myConnectedServers - 1) {
            endIdx = arrayToProcess.size();
        }
        chunks.push_back(vector<int>(
            arrayToProcess.begin() + startIdx,
            arrayToProcess.begin() + endIdx));
    }

    // Print each chunk
    for (int i = 0; i < (int)chunks.size(); ++i) {
        EV << "[Client " << clientId << "] chunk #" << i << ": ";
        for (int val : chunks[i]) {
            EV << val << " ";
        }
        EV << endl;
    }

    // 3) Send to 'myMajority' servers
    int toSend = std::min(myMajority, myConnectedServers);
    int gateUsedSoFar = 0;

    EV << "[Client " << clientId << "] Sending initial subtasks to " 
       << toSend << " server(s). (majority=" << myMajority << ")\n";

    for (int i = 0; i < gateSize("peer$o") && gateUsedSoFar < toSend; ++i) {
        cGate *outGate = gate("peer$o", i);
        if (!outGate->isConnected()) continue;

        cGate *otherGate = outGate->getPathEndGate();
        if (!otherGate) continue;
        cModule *otherMod = otherGate->getOwnerModule();
        const char *typeName = otherMod->getComponentType()->getName();

        // If it's a server
        if (strcmp(typeName, "Server") == 0) {
            // chunk = chunks[gateUsedSoFar]
            auto &chunk = chunks[gateUsedSoFar];
            SubtaskMessage *msg = new SubtaskMessage("Subtask");
            msg->setClientId(getIndex());
            msg->setDataArraySize(chunk.size());
            for (size_t j = 0; j < chunk.size(); ++j) {
                msg->setData(j, chunk[j]);
            }

            EV << "[Client " << clientId << "] Sending chunk #" 
               << gateUsedSoFar << " to gate " << i 
               << " -> " << typeName << " with " << chunk.size() << " numbers.\n";

            send(msg, "peer$o", i);
            gateUsedSoFar++;
        }
    }
}

//
// The main message handler
//
void Client::handleMessage(cMessage *msg)
{
    // 1) Self-message check
    if (msg->isSelfMessage()) {
        if (strcmp(msg->getName(), "StartRound2") == 0) {
            EV << "📣 [Client " << clientId << "] Starting Round 2\n";

            // Use the same approach as in sendInitialSubtasks() or do a new logic
            auto avgScores = calculateAverageScores();
            auto topServers = pickTopServers(myMajority); // pick top among the servers we connected

            // Create new array of 12 for Round 2
            vector<int> newArray;
            for (int i = 0; i < 12; ++i)
                newArray.push_back(intuniform(1, 100));

            // Split into myConnectedServers chunks
            // (similar approach as above)
            int chunkSize = newArray.size() / myConnectedServers;
            vector<vector<int>> chunks;
            for (int i = 0; i < myConnectedServers; ++i) {
                int startIdx = i * chunkSize;
                int endIdx   = (i+1) * chunkSize;
                if (i == myConnectedServers - 1)
                    endIdx = newArray.size();
                chunks.push_back(vector<int>(
                    newArray.begin() + startIdx,
                    newArray.begin() + endIdx));
            }

            // Send only to topServers (which are gate indices)
            // Note that pickTopServers() returns "gate indices" or "server IDs"?
            // We might have to unify that logic. 
            // If it returns gate indices, we can do:
            for (int gateIdx : topServers) {
                vector<int> &chunk = chunks[gateIdx % myConnectedServers]; 
                // or adapt to match your indexing approach

                SubtaskMessage *subtask = new SubtaskMessage("Subtask");
                subtask->setClientId(getIndex());
                subtask->setDataArraySize(chunk.size());
                for (size_t j = 0; j < chunk.size(); ++j) {
                    subtask->setData(j, chunk[j]);
                }
                send(subtask, "peer$o", gateIdx);
            }

            delete msg;
            return;
        }
    }

    // 2) GossipMessage
    if (auto *gossip = dynamic_cast<GossipMessage *>(msg)) {
        string hash = std::to_string(std::hash<std::string>{}(gossip->getContent()));
        if (messageLog.count(hash) == 0) {
            messageLog.insert(hash);
            EV << "🗣️ [Client " << clientId << "] Received GOSSIP from " 
               << gossip->getSenderId() << " -> " << gossip->getContent() << endl;

            // parse gossip
            string content = gossip->getContent();
            istringstream ss(content);
            string timestamp, senderClient, scores;
            getline(ss, timestamp, ':');
            getline(ss, senderClient, ':');
            getline(ss, scores, ':');

            map<int, int> serverScores;
            stringstream scoreStream(scores);
            string entry;
            while (getline(scoreStream, entry, '#')) {
                if (entry.empty()) continue;
                size_t eq = entry.find('=');
                if (eq != string::npos) {
                    int serverIdx = stoi(entry.substr(1, eq - 1));
                    int sc = stoi(entry.substr(eq + 1));
                    serverScores[serverIdx] = sc;
                }
            }
            allClientScores[senderClient] = serverScores;

            // Forward gossip to other peers (except the incoming gate)
            int fromGate = gossip->getArrivalGate()->getIndex();
            for (int i = 0; i < gateSize("peer$o"); ++i) {
                if (i != fromGate) {
                    send(gossip->dup(), "peer$o", i);
                }
            }
        }
        delete msg;
        return;
    }

    // 3) SubtaskMessage - clients shouldn't get these
    if (auto *subtask = dynamic_cast<SubtaskMessage *>(msg)) {
        EV << "⚠️ [Client " << clientId << "] Unexpected subtask received. Dropping.\n";
        delete msg;
        return;
    }

    // 4) ResultMessage - from servers
    auto *res = dynamic_cast<ResultMessage *>(msg);
    if (!res) {
        EV << "❌ [Client " << clientId << "] Unknown msg type. Dropping.\n";
        delete msg;
        return;
    }

    // store result
    int val = res->getResult();
    int fromGateIdx = res->getArrivalGate()->getIndex();

    resultsByGate[fromGateIdx].push_back(val);
    receivedResults++;

    EV << "📩 [Client " << clientId << "] Got result=" << val
       << " from gate=" << fromGateIdx 
       << " (received so far=" << receivedResults << ")\n";

    // check if we met threshold
    if (receivedResults >= myMajority) {
        // find majority result
        map<int,int> freq;
        for (auto &[gate, vals] : resultsByGate) {
            for (int v : vals)
                freq[v]++;
        }
        int majorityResult = -1;
        int maxCount = 0;
        for (auto &[xval, xcount] : freq) {
            if (xcount > maxCount) {
                maxCount = xcount;
                majorityResult = xval;
            }
        }

        // Score servers
        map<int, int> serverScores;
        for (auto &[gate, vals] : resultsByGate) {
            int sc = (vals[0] == majorityResult) ? 1 : 0;
            serverScores[gate] = sc;
            EV << "✅ [Client " << clientId << "] Gate=" << gate
               << " server scored=" << sc << endl;
        }

        // optional final result
        int finalRes = INT_MIN;
        for (auto &[gate, vals] : resultsByGate) {
            finalRes = max(finalRes, vals[0]);
        }
        EV << "🎯 [Client " << clientId << "] Final consolidated result=" << finalRes << endl;

        // Send gossip
        sendGossipMessage(serverScores);

        // reset for next round
        resultsByGate.clear();
        receivedResults = 0;

        // schedule round2 if not done
        if (!isRound2) {
            isRound2 = true;
            EV << "⏳ [Client " << clientId << "] Scheduling Round2 at t=" 
               << (simTime() + 5) << endl;
            cMessage *m = new cMessage("StartRound2");
            scheduleAt(simTime() + 5, m);
        }
    }

    delete msg;
}

//
// Broadcast scores as a gossip message
//
void Client::sendGossipMessage(map<int,int> serverScores)
{
    // e.g. 1679634442:C1:s0=1#s3=0#
    stringstream content;
    time_t now = time(nullptr);
    content << now << ":" << clientId << ":";

    for (auto &[gate, sc] : serverScores) {
        content << "s" << gate << "=" << sc << "#";
    }

    string msgBody = content.str();
    // store in messageLog so we don't re-forward our own gossip
    string hash = std::to_string(std::hash<std::string>{}(msgBody));
    messageLog.insert(hash);

    GossipMessage *gm = new GossipMessage("Gossip");
    gm->setSenderId(clientId.c_str());
    gm->setContent(msgBody.c_str());

    // send to all peers
    for (int i = 0; i < gateSize("peer$o"); i++) {
        send(gm->dup(), "peer$o", i);
    }
    delete gm;
}

//
// Averages all known server scores from 'allClientScores'
//
map<int,double> Client::calculateAverageScores()
{
    map<int,int> total, count;
    for (auto &[clientName, serverScs] : allClientScores) {
        for (auto &[serverGate, sc] : serverScs) {
            total[serverGate] += sc;
            count[serverGate]++;
        }
    }

    map<int,double> avg;
    for (auto &[srvGate, tot] : total) {
        avg[srvGate] = (double)tot / count[srvGate];
    }
    return avg;
}

//
// Picks top k servers by average score
// *But note* in this snippet, serverGate is the 'gate index' 
// Make sure that aligns with your chunking logic
//
vector<int> Client::pickTopServers(int k)
{
    auto avgs = calculateAverageScores();
    vector<pair<int, double>> vec(avgs.begin(), avgs.end());
    sort(vec.begin(), vec.end(), [](auto &a, auto &b){
        return a.second > b.second;
    });

    vector<int> top;
    for (int i = 0; i < (int)vec.size() && i < k; ++i) {
        top.push_back(vec[i].first); // server gate index
    }
    return top;
}