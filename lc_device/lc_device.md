# lc_device

This device is based on the ESP8266 based WeMos D1 module.
The device is mounted on the vehicle which passes under the infrared transmitter.
It retrieves it's configuration details from a telemetry host residing on the same subnet.

##### User Interaction
The user can interact with lc_device by connecting a terminal program to the
device USB interface. The Arduino IDE Serial Monitor is perfectly suited for
this purpose. Serial config is 96008N1.

#### Startup sequence
When lc_device reboots it will attempt to connect to WiFi using
the SSID already stored on device. If it fails to connect it will automatically
start scan for available SSIDs and present a selection to the user
via the terminal. The user can select a suitable SSID and will then be
prompted to enter the pass phrase.

After successful connection to the WiFi network the device will send a UDP
broadcast config request to the local subnet port 2000. The telemetry server
receives the request and replies with subnet broadcast of the configuration.
The reply contains the following details:
- http server domain, e.g. example.com.au
- http server port, e.g. 80
- http server page, e.g. lapcount?
- telemetry server port number
- telemetry server identifier

If the configuration relpy is not received after a timeout period the
lc_device will send another config request and stay in this loop until
a configuration has been received.

#### Device Functionality

#### Lapcount
The device is fitted with IR receiver. The receiver will trigger a device to
transmit a lapcount event to telemetry and http servers.
The server will reply with a confirmation packet to confirm the lap count
has reached it's destination.
Lapcount packets are sent with age information [in ms] to facilitate
time correction on the server side.

##### Lapcount http request
The request to the http service is GET HTTP/1.1