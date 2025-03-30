package assignment2;
simple Server
{
    parameters:
        string serverId = default("server%{index}");
        bool isMalicious;
        @display("i=block/server");
    gates:
        inout peer[];
}