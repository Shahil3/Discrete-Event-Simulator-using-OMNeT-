package assignment2;
simple Server
{
    parameters:
        string serverId;
        bool isMalicious = false; // default to honest
        @display("i=block/server");
    gates:
        inout peer[];
}