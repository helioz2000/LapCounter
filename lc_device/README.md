# ESP8266 Telemetry

A telemetry project to send data from a mobile device via ESP8266 to a host (e.g. Raspberry Pi).
The ESP8266 module used for this project is a WeMos D1 R1.

This project is designed for lap counting on RC race tracks and to provide telemetry data from the vehicles.

## Telemetry Host
The host sends a UDP broadcast package at a regular time interval (e.g. 10s) to facilitate automatic host address discovery by the client.
The broadcast data sent by the host consists of a single string with the following format:
 `LC1\txxxx\tHOSTNAME\tSERVERURL\tSERVERPORT\tSERVERPAGE` 

- \t = tab character as separator.
- LC1 = Identifier. Future version may use LC2, LC3, etc.
- xxxx = Port number where the host is listening for telemetry packets (e.g. 2006)
- HOSTNAME = the name of the host, for information only
- SERVERURL = http server domain, e.g. example.com.au
- SERVERPORT = http server port, e.g. 80
- SERVERPAGE = http server page, e.g. lapcount?

#### Broadcast interval
It is suggested to broadcast every 10 seconds so a new station can begin sending telemetry within 10s of coming online.

#### Device (Client) Functionality
Startup sequence:

- Establish WiFi access (Red LED flashes 250ms)
- Send Telemetry host request
- Wait for Telemetry host broadcast (Red LED flashes 1s)
- Send Logon UDP packet
- Continually send telemetry UDP packets to host
- Send Lapcount UDP packets to telemetry host
- Send Lapcount http requests to HTTP server

#### Lapcounting
The device is fitted with IR receiver. The receiver will trigger a device to transmit a lapcount event to telemetry and http servers.
The telemetry server will reply with a confirmation packet to confirm the lap count has reached it's destination.
Lapcount packets are sent with age information [in ms] to facilitate time correction on the server side.

## Telemetry packet structure
The packet is in ASCII format with tab separating the individual data fields.

| Field | Description|
|-------|:------------|
|1      | Packet Type (decimal number 1 digit) 
|2      | Packet Sequence Number 
|3      | Client ID (last 3 digits of MAC address) 

Data from field 4 onwards depends on the packet type:

**Packet Type 0:** Logon & Keep alive.

First packet to transmit after startup and successful discovery of telemetry host, thereafter sent at regular interval. Not required when telemetry data is sent.

| Field | Description
|-------|:------------
|4| Firmware version 0xAB, A=Major, B=Minor
|5| Logon indicator "L", only transmitted once after lchost discovery

**Packet Type 1:** Lap Count event.

| Field | Description
|-------|:------------
|4| time in ms since lap count signal has occured (long format)
|5| battery voltage in mV

This packet receives a reply from the telemetry host and is retransmitted if the host doesn't reply within 3 seconds
Each Lap Count packet is acknowledged by the host with a UDP packet to the client on the same port as the telemetry host port:

| Field | Description
|-------|:------------
|1      | Packet Type (decimal number 1 digit) 
|2      | Packet Sequence Number 
|3      | ACK (0x06) or NAK (0x15) 

**Packet Type 2:** Telemetry data

| Field | Description
|-------|:------------
|4      | Number of data items
|5      | Data  4 = 4 bytes or 2 = 2 bytes (low)
|6      | Data Type
|7      | Data
|8      | Data type
|9      | Data
|x      | pattern continues for all data items

Date type description:

| Type | Description
|-------|:------------
|1|Transponder Supply Voltage [mV]
|2|Solar Panel Voltage [mV]
|3|ESC Voltage [mV]
|4|Solar Current [mA]
|5|ESC Current [mA]