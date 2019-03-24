/*
 *  lc_device.ino
 *
 *  Hardware: WeMos D1 R1 (under ESP8266 boards)
 *  
 *  WiFi library doc: https://arduino-esp8266.readthedocs.io/en/latest/libraries.html#wifi-esp8266wifi-library
 *  
 *  Wiring:
 *  D3 - Lap count sensor, switches to ground.
 *  
 *  Functional description:
 *  
 *  When the lap count sensor input goes low a UDP packet is sent to the telemetry host
 *  with the time since the lapcount has occured. This packet will be re-transmitted
 *  until the host acknowledges the packet.
 *  
 *  Changing WiFi SSID and passphrase:
 *  At any time druing startup or operation the user can send "+++" to enter
 *  Network Discovery mode. After selection of a network the user is asked to enter
 *  the network passphrase.
 *  Once connected, the SSID and passphrase are stored in the module and used 
 *  durign subsequent WiFi activities.
 *   
 *  Pins:
 *  D0 = 16   // not working
 *  D1 = 5
 *  D2 = 4
 *  D3 = 0
 *  D4 = 2 (Blue LED_BUILTIN)
 *  D5 = 14
 *  D6 = 12
 *  D7 = 13
 *  D8 = 15
 */

const byte LAP_COUNT_SENSOR_PIN = 0;

#include <ESP8266WiFi.h>
#include <WifiUdp.h>
WiFiUDP Udp;
WiFiClient server;

#define UI Serial         // user interface

// Uncomment lines below to show diag info in terminal
//#define SHOWINFO_WIFIDIAG
//#define SHOWINFO_WIFICONNECTION
//#define SHOWINFO_ESP

ADC_MODE(ADC_VCC);    // switch analog input to read VCC

char wifi_ssid[32];                   // storage for WiFi SSID
char wifi_passphrase[32];;            // storage for WiFi passphrase
char wifi_hostname[12];               // storage for WiFi hostname (LCxxxxxx)
IPAddress wifi_broadcast_ip;          // broadcast Wifi IP address (calculated)
const unsigned long WIFI_CONNECT_TIMEOUT=15000;    // max connection time for WiFi timeout

// server 
String http_server_domain;
String http_server_file;
int http_server_port = 0;
bool http_server_enable = false;

// Infrared pulse detection parameters
unsigned long ir_pulse_start, ir_pulse_length;
const int IR_PULSE_MIN_LENGTH = 10;   //ms
bool ir_pulse_active = false;

typedef union {
  long l_value;
  uint8_t bytes[4];
} LONGUNION_t;

const bool LED_ON = false;
const bool LED_OFF = true;

const long TX_INTERVAL = 5000;        // telemetry TX
long nextTX;

IPAddress t_host_ip = (127,0,0,1);              // IP of telemetry host, set by host discovery 
const char t_host_id[] = {'L', 'C', '1'};       // ID for telemetry host (used host discovery)
const int T_HOST_ID_LEN = 3;                    // length of host id
bool t_host_enable = false;                     // Telemetry is enabled (host has been found)
const int telemetry_default_port = 2006;        // May be overridden by telemetry host discovery
unsigned int t_port = telemetry_default_port;   // Port for data exchange
const unsigned int bc_port = 2000;              // Broadcast port for telemetry host discovery
const long T_HOST_DISCOVERY_TIMEOUT = 15000;    // Timeout for telemetry host discovery

const int T_HOST_NAME_MAX_LEN = 30;
char t_host_name[T_HOST_NAME_MAX_LEN];    // storage for host name

int rxPacketSize;                       // bytes in received UDP packet
const int UDP_RX_BUFFER_SIZE = 256;
char rxPacket[UDP_RX_BUFFER_SIZE];      // buffer for incoming packets
char lcPacket[16];                      // buffer for lap count packet
char txPacket[256];                     // buffer for outgoing packets
bool t_listening = false;               // true when listening for telemetry host UDP packets
byte udp_sequence = 0;                  // sequence byte for UDP packets
const byte PACKET_TYPE_KEEP_ALIVE = 0;  // packet types 
const byte PACKET_TYPE_LAP_COUNT = 1;   // being sent 
const byte PACKET_TYPE_TELEMETRY = 2;   // to the telemetry host

volatile unsigned long lap_count_event_time = 0;  // set to curent millis reading on lap counter interrupt, reset to zero when processed 
bool lap_count_signal_shadow = false;
unsigned long lap_count_signal_block_time = 10000;     // ms for lap count sensor blocking (possible multiple signals)
unsigned long lap_count_signal_block_timeout;
//long lap_count_signal_time;               // time when lapcount event occured

uint8_t macAddr[6];                       // MAC address of this device

char inputBuffer[32];                     // used for serial port input from user

volatile byte flash_byte = 0;             // used by ISR to provide pulsing bits

#define FLASH_31 0
#define FLASH_62 1
#define FLASH_125 2
#define FLASH_250 3
#define FLASH_500 4
#define FLASH_1S 5
#define FLASH_2S 6
#define FLASH_4S 7

// ********************************************************************************
//    Interrupt routines
// ********************************************************************************

/*
 * Interrupt Service Routine
 * Timer triggered interrrupt
 */
void ICACHE_RAM_ATTR onTimerISR() {
  flash_byte++;
}

/*
 * Interrupt Service Routine (disabled!)
 * triggered by a signal on lap counter digital input
 */
void ICACHE_RAM_ATTR onLapCountISR() {
  lap_count_event_time = millis();
}

// ********************************************************************************
//    Setup and Loop functions
// ********************************************************************************

void setup() {
  UI.begin(9600);
  delay(10);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LAP_COUNT_SENSOR_PIN, INPUT_PULLUP);

  // get MAC address to be used as ID
  WiFi.macAddress(macAddr);

  // set hostname
  sprintf(wifi_hostname, "LC%02X%02X%02X", macAddr[3], macAddr[4], macAddr[5]);
  mylog("Setting this hostname to %s", wifi_hostname);
  WiFi.hostname(wifi_hostname);
  
  // We start by connecting to a WiFi network
  mylog("\n\n\nEnter +++ to activate WiFi config mode.\nConnecting to %s", WiFi.SSID().c_str());
  
  WiFi.begin();
  
  digitalWrite(LED_BUILTIN, LED_ON);

  // configure time interrupt
  timer1_attachInterrupt(onTimerISR);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);   // 5 ticks / us
  timer1_write( 31250 * 5); //31250 us

  // configure lap counter input interrupt
  // disabled as it picks up sporadic pulses collected by the IR sensor board.
  //attachInterrupt(digitalPinToInterrupt(LAP_COUNT_SENSOR_PIN), onLapCountISR, FALLING); 
}

void loop() {
  // establish WiFi if not connected
  if (WiFi.status() != WL_CONNECTED) {
    wait_for_wifi();
  }

  // check for user input
  if (scan_user_input()) {
    wifi_select_network();
    wait_for_wifi();
  }

 // send telemetry at interval
  if (millis() >= nextTX) {
    nextTX += TX_INTERVAL;
    send_telemetry_keepalive();
  }

  process_ir_signal();
  
  // process lapcount event
  if (lap_count_event_time != 0) {
    process_lap_count();
  }

  // receive UDP packets from host
  if (t_listening) {
    rxPacketSize = Udp.parsePacket();
    if (rxPacketSize) {
      process_rx_packet();
    }
  }
}

/*
 * process lap count event
 * lap countign is blocked for a period of time to avoid multiple counts
 * when we receive multiple close spaced pulses.
 */
void process_lap_count() {
  if (!lap_count_signal_shadow) {
    mylog("Lap Count Sensor Interrupt <%d>\n", digitalRead(LAP_COUNT_SENSOR_PIN));
    lap_count_signal_shadow = true;
    lap_count_signal_block_timeout = millis() + lap_count_signal_block_time;
    send_lapcount_udp();
    send_lapcount_http(lap_count_event_time);     
  } else {
    if(millis() >= lap_count_signal_block_timeout) {
      lap_count_event_time = 0;
      lap_count_signal_shadow = false;
    }
  } 
}

/*
 * process infrared sensor signal
 * - measure pulse length
 * - trigger lap count event
 * minimum pulse length must be reached before lap counter event is triggered.
 * this prevents false triggers due to spurious pulses.
 */
void process_ir_signal() {
    // measure pulse length
  if (ir_pulse_active) {
    // detect trailing edge
    if (digitalRead(LAP_COUNT_SENSOR_PIN)) {
      ir_pulse_active = false;
      // report pulse length for valid pulses only
      if (ir_pulse_length > IR_PULSE_MIN_LENGTH) {
        mylog("Lap Count Sensor pulse length: %dms\n", ir_pulse_length);
      }
    } else {
      // while pulse is active calculate current pulse length
      ir_pulse_length = millis() - ir_pulse_start;
      // has pulse reached min length?
      if (ir_pulse_length >= IR_PULSE_MIN_LENGTH) {
        // pulse start time is the lap count event time
        lap_count_event_time = ir_pulse_start;
        //mylog("Lap Count Sensor pulse length: %dms\n", ir_pulse_length);
      }
    } 
  }

  // IR pulse detect start when input goes low
  if (!digitalRead(LAP_COUNT_SENSOR_PIN) && !ir_pulse_active) {
    ir_pulse_start = millis();
    ir_pulse_active = true;
    ir_pulse_length = 0;
  }
 
}

/*
 * process received UDP packet
 */
void process_rx_packet() {
  mylog("Received %d bytes\n", rxPacketSize);
  Udp.read(rxPacket, rxPacketSize);
  
  // show UDP packet contents
  for (int i = 0; i<rxPacketSize; i++) {
    mylog("[%0X] ", rxPacket[i]);
  }
  UI.println();
}

// ********************************************************************************
//    Lap Count server functions
// ********************************************************************************

bool send_lapcount_http(unsigned long event_time) {
  int line_no = 0;
  char *token;
  char line_str[32];
  int response_status_code;
  if (!http_server_enable) return false;
  unsigned long http_start_time = millis();
  mylog("Sending lapcount http request .... \n");
  if (! server.connect(http_server_domain.c_str(), http_server_port)) {
    mylog("lap count http error: connection to <%s:%d> failed\n", http_server_domain.c_str(), http_server_port);
    server.stop();
    return false;
  }
  
  //mylog("connected to %s\n", serverName);
  // construct data string
  // 1: our wifi hostname
  String httpStr = String(wifi_hostname);
  // 2: event age
  httpStr += "+";
  httpStr += String((millis() - event_time));

  // send string to server encapsulated in HTTP GET request
  server.print(String("GET /") + http_server_file + httpStr + " HTTP/1.1\r\n" + "Host: " + http_server_domain + "\r\n" + "Connection: close\r\n" + "\r\n");
  //mylog("[Response:]\n");
  while (server.connected() || server.available()) {
    if (server.available()) {
      String line = server.readStringUntil('\n');
      line_no++;
      // check first line for reply success
      if (line_no == 1) {
        strncpy(line_str,line.c_str(), 31);
        // look for second space separated token
        token = strtok(line_str, " "); token = strtok(NULL, " ");
        response_status_code = atoi(token);
        //mylog("token: %s\n",token);
        // check response status is OK
        if ( (response_status_code < 200) || (response_status_code > 299) ) {
          mylog("error response received from server: <%s>\n", line.c_str());
          break;
        } else {
          //mylog("http server response: %d\n", response_status_code);
        }
      }
      
      // look for reply LC:
      if ( (line[0] == 'L') && (line[1] == 'C') && (line[2] == ':') ) {
        //mylog("--->> %s", line.c_str());
        line.remove(2,1);
        if (line.compareTo(String(wifi_hostname)) == 0) {
          //mylog("lap count http ACK received\n");
        } else {
          mylog("lap count http error - received: <%s>, looking for: <%s>]\n", line.c_str(), wifi_hostname); 
        }
      }
      //mylog("%s\n",line.c_str());
    }
  }
  server.stop();
  //mylog("\n[Disconnected]\n");
  mylog("HTTP execution time: %dms\n", millis()-http_start_time);
  return true;
}

bool send_lapcount_udp() {
  char strbuf[16];
  LONGUNION_t elapsed_time;

  if (!t_host_enable) return false;
  
  int packet_length = make_telemetry_header(PACKET_TYPE_LAP_COUNT);
  elapsed_time.l_value = millis() - lap_count_event_time;
  //lap_count_event_time -= 255;
  sprintf(strbuf, "\t%d\n", elapsed_time);
  strcat(txPacket, strbuf);
  mylog("lapcount UDP: %s", txPacket);
  return send_telemetry_packet(strlen(txPacket));

}

// ********************************************************************************
//    Telemetry functions
// ********************************************************************************

/*
 * send keepalive packet
 */
void send_telemetry_keepalive() {
  int packet_length = make_telemetry_header(PACKET_TYPE_TELEMETRY);
  strcat(txPacket, "\n");
  send_telemetry_packet(strlen(txPacket));
}

/*
 * send telemetry data to host
 */
bool send_telemetry_packet(int packet_length) {
  bool retVal = false;
  if (!t_host_enable) return retVal;
  if (packet_length < 1) return retVal;
  if (!Udp.beginPacket(t_host_ip, t_port)) {
    UI.println("Telemetry: Udp.beginPacket failed");
    goto send_done;
  }
   
  digitalWrite(LED_BUILTIN, LED_ON);
  if ( Udp.write(txPacket, packet_length) != packet_length)  {
    UI.println("Telemetry: Udp.write failed");
    goto send_done;
  }
  
  if (!Udp.endPacket()) {
    UI.println("Telemetry: Udp.endPacket failed");
  } else {
    //mylog("%d: Telemetry Packet %d sent\n", millis(), udp_sequence-1);
    retVal = true;
  }
  
send_done:
  digitalWrite(LED_BUILTIN, LED_OFF);
  return retVal;
}

/*
 * make telemetry header
 * returns length of assembled header in txPacket variable
 */
int make_telemetry_header(byte packet_type) {
  sprintf(txPacket, "%d\t%d\t%02X%02X%02X", packet_type, udp_sequence++, macAddr[3], macAddr[4], macAddr[5] );
  return strlen(txPacket);
}

// ********************************************************************************
//    Telemetry Host functions
// ********************************************************************************
/*
 * setup listener on telemerty port for replys from telemetry host.
 * returns true on success
 */
bool setup_t_port_listening () {
  if (Udp.begin(t_port) != 1) {
    mylog("!! ERROR: unable to listen on UPD port %d\n",t_port);
    return false;
  }
  t_listening = true;
  mylog("Listening on local UPD port %d\n",t_port);
  return true;
}

bool send_host_discovery_request() {
  int packet_length;

  // send config request
  if (!Udp.beginPacket(wifi_broadcast_ip, bc_port)) {
    UI.println("Telemetry1: Udp.beginPacket failed");
    goto send_done;
  }
   
  digitalWrite(LED_BUILTIN, LED_ON);

  sprintf(txPacket, "%s\n", wifi_hostname);
  packet_length = strlen(txPacket);
  
  if ( Udp.write(txPacket, packet_length) != packet_length)  {
    UI.println("Telemetry1: Udp.write failed");
  }

send_done:

  if (!Udp.endPacket()) {
    UI.println("Telemetry1: Udp.endPacket failed");
  } 

  digitalWrite(LED_BUILTIN, LED_OFF);

}

/*
 * Wait for broadcast from telemetry host
 * timeout: timeout in ms
 * returns: true on success and false on fail
 */
bool discover_telemetry_host(long timeout) {
  bool retval = false;
  int retry_count = 0;
  long timeout_value;
  int packetSize;
  int bytesRead;

retry_loop:
  
  // exit if WiFi is not connected
  if (WiFi.status() != WL_CONNECTED) {
    goto end_loop;
  }

  send_host_discovery_request();

  // Start listening on broadcast port
  if (Udp.begin(bc_port) != 1) {
    goto end_loop;
  } 
  
  timeout_value = millis() + timeout;
  bytesRead = 0;

  // wait for broadcast packet from telemetry host
  while (millis() < timeout_value) {
    packetSize = Udp.parsePacket();
    if(packetSize) {
       // read the packet into packetBufffer
      bytesRead = Udp.read(rxPacket,UDP_RX_BUFFER_SIZE);    
      if (validateTelemetryHost(bytesRead)) {
        Udp.flush();
        Udp.stop();
        setup_t_port_listening();
        retval = true;
        goto end_loop;
      }
    }
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_250));
    // Check for user input which indicates the user wants to change WiFi network
    if (UI.available()) goto end_loop;
  }
  Udp.stop();
  // keep retrying until we find a telemetry host
  retry_count++;
  mylog("Telemetry host discover timeout, retry %d\n", retry_count);
  goto retry_loop;

end_loop:
  digitalWrite(LED_BUILTIN, LED_OFF);
  return retval;
}

/*
 * Validate UDP packet from telemetry broadcast t
 * returns: true if host is valid, otherwise false
 */
bool validateTelemetryHost(int bufsize) {
  // ID sufficient length?
  if (bufsize < T_HOST_ID_LEN) {
    return false;
  }
  // check ID contents
  for (int i=0; i<T_HOST_ID_LEN; i++) {
    if (rxPacket[i] != t_host_id[i]) return false;
  }
  // We have a valid ID, record the host IP
  t_host_ip = Udp.remoteIP();
  t_host_enable = true;
  mylog("Telemetry host IP: ");
  UI.println(t_host_ip);

  // check packet for more host information
  rxPacket[bufsize] = 0;  // force end of string
  char *token = strtok(rxPacket, "\t");
  int tokencount = 0;
  unsigned int port;
  while (token != 0) {
    tokencount++;
    switch(tokencount) {
      case 1:   // Host Identifier (e.g. "LC1")
        mylog("Telemetry host ID: <%s>\n", token);
        break;
      case 2:   // Telemetry port (on telemetry server)
        port = atoi(token);
        if (port > 0 && port <= 65535 ) {
          t_port = port;
        } else {
          t_port = telemetry_default_port;
        }
        mylog("Telemetry port: %d\n",  t_port);
        break;
      case 3:   // Hostname of telemetry broadacst server
        strncpy(t_host_name, token, T_HOST_NAME_MAX_LEN);
        mylog("Telemetry host: %s\n", t_host_name );
        break;
      case 4:   // HTTP server domain ("support.rossw.net")
        http_server_domain = String(token);       
        break;
      case 5:   // HTTP server port (80)
        http_server_port=atoi(token);
        break;
      case 6:   // HTTP server page ("testpage?")
        http_server_file = String(token);
        http_server_enable = true;
        mylog("HTTP Server: %s:%d/%s\n", http_server_domain.c_str(), http_server_port, http_server_file.c_str() );
        break;
      default:
        // unknown token
        break;
    }
    token = strtok(0, "\t");
  }
  
  return true;
}


// ********************************************************************************
//    WiFi functions
// ********************************************************************************

/*
 * Allow user to change WiFi SSID and password
 */
void wifi_select_network() { 
  byte numSsid;
  int thisNet;
  
startAgain:
  UI.println("\n** Scanning Nearby Networks **");
  // scan for nearby networks:
  numSsid = WiFi.scanNetworks();
  // print the list of networks seen:
  mylog("SSID List: [%d]\n", numSsid);
  // print the network number and name for each network found:
  for (thisNet = 0; thisNet<numSsid; thisNet++) {
    mylog("%d) [%ddBm] %s\n", thisNet, WiFi.RSSI(thisNet), WiFi.SSID(thisNet).c_str() );
  }
  mylog("Select Network [0-%d] and press Enter: ", numSsid-1);
  if (read_line() <= 0) {
    mylog("Error - Nothing selected\n");
    goto startAgain;
  }
  mylog("%s\n", inputBuffer);
  thisNet = atoi(inputBuffer);
  if ( (thisNet >= numSsid)  || (thisNet < 0)) {
    mylog("Error - Invalid selection\n");
    goto startAgain;
  }
  mylog("Please enter pass phrase for %s : ", WiFi.SSID(thisNet).c_str() );
  if (read_line() < 0) {
    mylog("Error - A valid passphrase must be entered\n");
    goto startAgain;
  }
  mylog("**passphrase**\n\nConnecting to %s\n", WiFi.SSID(thisNet).c_str());
  // save ssid and passphrase
  strcpy(wifi_ssid, WiFi.SSID(thisNet).c_str());
  strcpy(wifi_passphrase, inputBuffer);
  
  // connect usign new credentials
  WiFi.disconnect(true);                  // this will clear the previous credentials
  WiFi.begin(wifi_ssid, wifi_passphrase);
}

/*
 * Wait for WiFi to connect
 * If not connected within timeout period the user will be prompted to select a new WiFi network
 * Once the WiFi is conneced we wait for a broadcast packet from the telemetry host 
 */
void wait_for_wifi() {
  long timeout;
start_again:  
  timeout = millis() + WIFI_CONNECT_TIMEOUT;
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);             // do not remove, no delay will crash the ESP8266
    digitalWrite(LED_BUILTIN, bitRead(flash_byte, FLASH_1S));
    if (millis() >= timeout) {
      mylog("\nWiFi timeout trying to connect to %s\n", WiFi.SSID().c_str());
      wifi_select_network();
      timeout = millis() + WIFI_CONNECT_TIMEOUT;
    }
    if (scan_user_input()) {
      wifi_select_network();
      timeout = millis() + WIFI_CONNECT_TIMEOUT;
    }
  }
  digitalWrite(LED_BUILTIN, LED_OFF);
  calculate_broadcast_ip();
  //mylog("WiFi Connected, [%02X:%02X:%02X:%02X:%02X:%02X]\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);

#ifdef SHOWINFO_WIFIDIAG
  UI.println("");
  WiFi.printDiag(Serial);
#endif

  show_wifi_info();
  esp_info();

  mylog("Telemetry host discovery in progress ...\n");

  if (discover_telemetry_host(T_HOST_DISCOVERY_TIMEOUT)) {
    //mylog("Telemetry host found (%d.%d.%d.%d)\n", t_host_ip[0], t_host_ip[1], t_host_ip[2], t_host_ip[3]);
    nextTX = millis() + TX_INTERVAL;
  } else {
    mylog("Failed to find telemetry host\n");
  }
  // allow user to interrupt and select different network
  if (scan_user_input()) {
    UI.println(" ");
    wifi_select_network();
    goto start_again;
  }
}


// ********************************************************************************
//    Utility functions
// ********************************************************************************

/*
 * Scan for user input of +++ to enter WiFi config mode
 * returns true if the user has entered +++ otherwise false
 */
bool scan_user_input() {
  if (UI.available()) {
    if (read_line() == 3) {
      for (int i=0; i<3; i++) {
        if (inputBuffer[i] != '+') return false;
      }
      // drain the input buffer
      while (UI.available()) {
        UI.read();
      }
      return true;
    }
  }
  return false;
}

/*
 * read one line of user input into buffer 
 * returns the number of characters read into the input buffer
 */
int read_line() {
  int cnt = 0;
  char c;
  while (1) {
    if (UI.available()) {
      c = UI.read();
      if (c >= 0x1F) {
        inputBuffer[cnt++] = c;
      }
      if ((c == '\n') || (cnt == sizeof(inputBuffer)-1)) {
        inputBuffer[cnt] = '\0';
        return cnt;    
      }
    }
  }
}


/* 
 *  print debug output on console interface
 */
void mylog(const char *sFmt, ...)
{
  char acTmp[128];       // place holder for sprintf output
  va_list args;          // args variable to hold the list of parameters
  va_start(args, sFmt);  // mandatory call to initilase args 

  vsprintf(acTmp, sFmt, args);
  UI.print(acTmp);
  // mandatory tidy up
  va_end(args);
  return;
}

/*
 * Calculate the network broadcast address from IP address and subnet mask
 */
void calculate_broadcast_ip() {
  byte mask;
  byte b_cast[4];
  IPAddress ip = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();
  for (int i = 0; i < 4; i++) {
    mask = ~subnet[i];
    wifi_broadcast_ip[i] = ip[i] | mask;
  }  
}

void show_wifi_info() {
  if (WiFi.status() == WL_CONNECTED) {
    mylog("\nWiFi status: Connected\n");
    uint8_t macAddr[6];
    WiFi.macAddress(macAddr);
    mylog("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n", macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
    IPAddress ip = WiFi.localIP();
    mylog("IP address: %s\n", ip.toString().c_str() );
    ip = WiFi.subnetMask();
    mylog("Subnet mask: %s\n", ip.toString().c_str() );
    mylog("Hostname: %s\n", WiFi.hostname().c_str() );
    mylog("RSSI: %d dBm\n", WiFi.RSSI() );
  } else {
    mylog("\nWiFi status: Not Connected\n");
  }

}

void esp_info() {
#ifdef SHOWINFO_ESP
  mylog("\nChip info:\n");
  mylog("Reset reason: %s\n", ESP.getResetReason().c_str() );
  mylog("Chip ID: %u\n", ESP.getChipId() );
  mylog("Core Version: %s\n", ESP.getCoreVersion().c_str() );
  mylog("SDK Version: %s\n", ESP.getSdkVersion() );
  mylog("CPU Frequency: %uMHz\n", ESP.getCpuFreqMHz() );
  mylog("Sketch size: %u\n", ESP.getSketchSize() );
  mylog("Free Sketch space: %u\n", ESP.getFreeSketchSpace() );
  mylog("Flash Chip ID: %u\n", ESP.getFlashChipId() );
  mylog("Flash Chip size: %u (as seen by SDK)\n", ESP.getFlashChipSize() );
  mylog("Flash Chip size: %u (physical)\n", ESP.getFlashChipRealSize() );
  mylog("Flash Chip speed: %uHz\n", ESP.getFlashChipSpeed() );
  mylog("VCC: %.2fV\n", (float)ESP.getVcc() / 896 );
#endif
}
