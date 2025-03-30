package assignment2;
simple Client
{
    parameters:
        string clientId = default("client%{index}");
        @display("i=block/client");
    gates:
        inout peer[]; // allows connecting to multiple servers/clients
}