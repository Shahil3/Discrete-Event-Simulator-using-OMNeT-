package assignment2;

network DistributedNetwork
{
    parameters:
        int numClients;
        int numServers;

    submodules:
        client[numClients]: Client {
            parameters:
                clientId = default("client" + string(index));
        }
        server[numServers]: Server {
            parameters:
                serverId = default("server" + string(index));
        }

    connections allowunconnected:
}