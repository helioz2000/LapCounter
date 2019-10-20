# ESP8266_Telemetry

A telemetry project to send data from a mobile device via ESP8266 to a host (e.g. Raspberry Pi).
The ESP8266 module used for this project is a WeMos D1 R1.

This project is designed for lap counting on RC race tracks and to provide telemetry data from the vehicles.

#### Telemetry Host
The host sends a UDP broadcast package at a regular time interval (e.g. 10s) to facilitate automatic host address discovery by the client.
The broadcast data sent by the host consists of a single string with the following format:
> "LC1\t2006\tHOSTNAME\tSERVERURL\tSERVERPORT\tSERVERPAGE" 

- \t = tab character as separator.
- LC1 = Identifier. Future version may use LC2, LC3, etc.
- 2006 = Port number where the host is listening for telemetry packets
- HOSTNAME = the name of the host, for information only
- SERVERURL = http server domain, e.g. example.com.au
- SERVERPORT = http server port, e.g. 80
- SERVERPAGE = http server page, e.g. lapcount?

##### Broadcast interval
It is suggested to broadcast every 10 seconds so a new station can begin sending telemetry within 10s of coming online.

#### Client Functionality
Client startup sequence:

- Establish WiFi access (Red LED flashes 250ms)
- Wait for Telemetry host broadcast (Red LED flashes 1s)
- Continually send telemetry UDP packets to host
- Send Lapcount packets to telemetry host
- Send Lapcount http requests to HTTP server

#### Lapcount
The device is fitted with IR receiver. The receiver will trigger a device to transmit a lapcount event to telemetry and http servers.
The server will reply with a confirmation packet to confirm the lap count has reached it's destination.
Lapcount packets are sent with age information [in ms] to facilitate time correction on the server side.

##### Telemetry packet structure
The packet is in ASCII format with tab separating the individual data fields.

| Field | Description.|
|-------|:------------|
|1 | Packet Type (decimal number 1 digit) |
|2| Packet Sequence Number |
|3| Client ID (last 3 digits of MAC address) |

Data from field 4 onwards depends on the packet type:

**Packet Type 0:** Logon & Keep alive.
First packet to transmit after startup and successful discovery of telemetry host, thereafter sent at regular interval. Not required when telemetry data is sent.

| Field | Description.
|-------|:------------
|4| Firmware version 0xAB, A=Major, B=Minor

**Packet Type 1:** Lap Count event.

| Field | Description.
|-------|:------------
|4| time in ms since lap count signal has occured (long format)

This packet receives an acknowledgement from the telemetry host and is retransmitted if the host doesn't reply within 3 seconds
Each Lap Count packet is acknowledged by the host with a UDP packet to the client on the same port as the telemetry host port:

| Field | Description.
|-------|:------------
|5      | ACK (0x06) or NAK (0x15) 
|6      | sequence number 

**Packet Type 2:** Telemetry data

| Field | Description.
|-------|:------------
|4      | Number of data items
|5      | Data  4 = 4 bytes or 2 = 2 bytes (low)
|6      | Data Type
|7      | Data
|8      | Data type
|9      | Data
|x      | pattern continues for all data items
