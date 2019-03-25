# Lap Counter Device (lc_device)

This device is based on the ESP8266 based WeMos D1 module.
The device is mounted on the vehicle which passes in view of the infrared transmitter.
It retrieves it's configuration details from a telemetry host residing on the same subnet.

![Lap Counter Device](/lc_device.jpg)

##### User Interaction
The user can interact with lc_device by connecting a terminal program to the
device USB interface. The Arduino IDE Serial Monitor is perfectly suited for
this purpose. Serial config is 9600,8N1.

#### Startup sequence
When lc_device (re)boots it will attempt to connect to WiFi using
the SSID already stored on device. If it fails to connect it will automatically start scanning for available SSIDs and present a selection to the user via the terminal. The user can select a suitable SSID and will then be prompted to enter the pass phrase.

To change/replace an existing [already connected] WiFi connection send 
> `+++` Enter

to trigger scan for SSID's followed by pass phrase entry. 

After successful connection to the WiFi network the device will send a UDP broadcast config request to the local subnet port 2000. The telemetry server receives the request and responds with a subnet broadcast of the configuration.

The response contains the following details:

- `http server domain`, e.g. example.com.au
- `http server port`, e.g. 80
- `http server page`, e.g. lapcount?
- `telemetry server port number` e.g. 2006
- `telemetry server identifier` e.g. LC1

If the configuration response is not received after a timeout period the
lc_device will send another config request and stay in this loop until
a configuration has been received (there is no point in proceeding).

### Device Functionality

#### Blue LED on Wemos board
During startup the blue LED:
- flashes slow (1s) while trying to connect to WiFi
- flashes fast (250mS) while waiting for Telemetry host
- OFF after successful startup
- flashes briefly during telemetry transmission

#### Lapcount
The device is fitted with IR receiver. The receiver will trigger a device to transmit a lapcount event to telemetry and http servers.
The server will reply with a confirmation packet to confirm the lap count has reached it's destination. Lapcount packets are sent with age information [in ms] to facilitate time correction on the server side.

##### Lapcount http request
The request to the http service is GET HTTP/1.1
> server.example.com/lapcount?deviceID+eventAge+batVoltage

- deviceID = `LC112233` (112233 are the last 3 octets of the Wemos D1 MAC address)
- eventAge = `1234` (number of milliseconds since the lap count event occured)
- batVoltage = `3100` (millivolts of receiver battery)

The http server reply is verified by the device:

- http result code must be between 200 and 299
- the reply must contain "LC:112233" where 112233 matches the device ID on the request.

It is the server's responsibility to keep track of the number of lap count events, to log them, etc.  
