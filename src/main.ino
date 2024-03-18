#include <FS.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#define TRIGGER_PIN 0
#include <SPIFFS.h>
#include <ArduinoJson.h>

#include <TinyGPSPlus.h>
#include <SocketIoClient.h>

#include <LoRa.h>
#include <SPI.h>
// wifimanager can run in a blocking mode or a non blocking mode
// Be sure to know how to process loops with no delay() if using non blocking
bool wm_nonblocking = false; // change to true to use non blocking

#define ss 5
#define rst 14
#define dio0 2

int counter = 0;

TinyGPSPlus gps;
SocketIOclient socketIO;

char deviceid[10];
char server[50] = "http://localhost";
char port[6] = "8080";
char api_token[34] = "YOUR_API_TOKEN";
// flag for saving data
bool shouldSaveConfig = false;

WiFiManager wm; // global wm instance
// WiFiManagerParameter custom_deviceid; // global param ( for non blocking w params )
// WiFiManagerParameter custom_server;   // global param ( for non blocking w params )
// WiFiManagerParameter custom_port;     // global param ( for non blocking w params )

void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case sIOtype_DISCONNECT:
        Serial.printf("[IOc] Disconnected!\n");
        break;
    case sIOtype_CONNECT:
        Serial.printf("[IOc] Connected to url: %s\n", payload);

        // join default namespace (no auto join in Socket.IO V3)
        socketIO.send(sIOtype_CONNECT, "/");
        break;
    case sIOtype_EVENT:
    {
        char *sptr = NULL;
        int id = strtol((char *)payload, &sptr, 10);
        Serial.printf("[IOc] get event: %s id: %d\n", payload, id);
        if (id)
        {
            payload = (uint8_t *)sptr;
        }
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error)
        {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.c_str());
            return;
        }

        String eventName = doc[0];
        Serial.printf("[IOc] event name: %s\n", eventName.c_str());
        if (eventName == "datafromserver")
        {
            Serial.println("datafromserver");
            int data = doc[1];
            Serial.printf("data: %d\n", data);
        }
    }
    break;
    case sIOtype_ACK:
        Serial.printf("[IOc] get ack: %u\n", length);
        break;
    case sIOtype_ERROR:
        Serial.printf("[IOc] get error: %u\n", length);
        break;
    case sIOtype_BINARY_EVENT:
        Serial.printf("[IOc] get binary: %u\n", length);
        break;
    case sIOtype_BINARY_ACK:
        Serial.printf("[IOc] get binary ack: %u\n", length);
        break;
    }
}

void mountSPIFFS()
{
    if (SPIFFS.begin(true))
    {
        Serial.println("MOUNTED FILE SYSTEM");
        if (SPIFFS.exists("/config.json"))
        {
            // file exists, reading and loading
            Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile)
            {
                Serial.println("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError)
                {
                    Serial.println("\nparsed json");
                    strcpy(deviceid, json["deviceid"]);
                    strcpy(server, json["server"]);
                    strcpy(port, json["port"]);
                }
                else
                {
                    Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
    }
    else
    {
        Serial.println("FAILED TO MOUNT SPIFF");
    }
}

void setup()
{
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    Serial.begin(115200);

    while (!Serial)
        ;

    LoRa.setPins(ss, rst, dio0);
    Serial.println("LoRa Receiver");
    if (!LoRa.begin(433E6))
    { // or 915E6
        Serial.println("Starting LoRa failed!");
        while (1)
            ;
    }

    // gps module
    //  Serial2.begin(9600);

    Serial.setDebugOutput(true);

    // loraconfig

    // read configuration from FS json
    Serial.println("mounting FS...");

    mountSPIFFS();

    delay(3000);

    Serial.println("\n Starting");

    pinMode(TRIGGER_PIN, INPUT);

    // wm.resetSettings(); // wipe settings

    if (wm_nonblocking)
        wm.setConfigPortalBlocking(false);

    // add a custom input field
    // int customFieldLength = 100;

    WiFiManagerParameter custom_deviceid("deviceid", "Device ID", "", 10, "placeholder=\"0001\"");
    WiFiManagerParameter custom_server("server", "Server Addr", "", 50, "placeholder=\"192.168.1.1\"");
    WiFiManagerParameter custom_port("port", "Port", "", 6, "placeholder=\"3000\"");

    wm.addParameter(&custom_deviceid);
    wm.addParameter(&custom_server);
    wm.addParameter(&custom_port);
    wm.setSaveParamsCallback(saveParamCallback);

    // custom menu via array or vector
    //
    // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
    // const char* menu[] = {"wifi","info","param","sep","restart","exit"};
    // wm.setMenu(menu,6);
    std::vector<const char *> menu = {"param", "wifi", "sep", "info", "restart", "exit"};
    wm.setMenu(menu);

    // set dark theme
    wm.setClass("invert");

    // set static ip
    //  wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
    //  wm.setShowStaticFields(true); // force show static ip fields
    //  wm.setShowDnsFields(true);    // force show dns field always

    // wm.setConnectTimeout(20); // how long to try to connect for before continuing
    wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
    // wm.setCaptivePortalEnable(false); // disable captive portal redirection
    // wm.setAPClientCheck(true); // avoid timeout if client connected to softap

    // wifi scan settings
    // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
    // wm.setMinimumSignalQuality(20);  // set min RSSI (percentage) to show in scans, null = 8%
    // wm.setShowInfoErase(false);      // do not show erase button on info page
    // wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons

    // wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

    bool res;
    // res = wm.autoConnect(); // auto generated AP name from chipid
    // res = wm.autoConnect("AutoConnectAP"); // anonymous ap
    res = wm.autoConnect("ESP32DeviceConfigAP", "password"); // password protected ap

    if (!res)
    {
        Serial.println("Failed to connect or hit timeout");
        // ESP.restart();
    }
    else
    {
        // if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");

        // read updated parameters
        strcpy(deviceid, custom_deviceid.getValue());
        strcpy(server, custom_server.getValue());
        strcpy(port, custom_port.getValue());
        Serial.println("The values in the file are: ");
        Serial.println("deviceid : " + String(deviceid));
        Serial.println("server : " + String(server));
        Serial.println("port : " + String(port));

        const char *sockethost = "192.168.190.203";
        int socketport = atoi("3000");
        socketIO.begin(sockethost, socketport, "/socket.io/?EIO=4");
        socketIO.onEvent(socketIOEvent);

        // save the custom parameters to FS
        if (shouldSaveConfig)
        {
            Serial.println("saving config");
            DynamicJsonDocument json(1024);
            json["deviceid"] = deviceid;
            json["server"] = server;
            json["port"] = port;
            File configFile = SPIFFS.open("/config.json", "w");
            if (!configFile)
            {
                Serial.println("failed to open config file for writing");
            }

            serializeJson(json, Serial);
            serializeJson(json, configFile);
            configFile.close();
            // end save
        }
    }
}

void checkButton()
{
    // check for button press
    if (digitalRead(TRIGGER_PIN) == LOW)
    {
        // poor mans debounce/press-hold, code not ideal for production
        delay(50);
        if (digitalRead(TRIGGER_PIN) == LOW)
        {
            Serial.println("Button Pressed");
            // still holding button for 3000 ms, reset settings, code not ideaa for production
            delay(3000); // reset delay hold
            if (digitalRead(TRIGGER_PIN) == LOW)
            {
                Serial.println("Button Held");
                Serial.println("Erasing Config, restarting");
                wm.resetSettings();
                ESP.restart();
            }

            // start portal w delay
            Serial.println("Starting config portal");
            wm.setConfigPortalTimeout(120);

            if (!wm.startConfigPortal("ESP32ConfigOnDemandAP", "password"))
            {
                Serial.println("failed to connect or hit timeout");
                delay(3000);
                // ESP.restart();
            }
            else
            {
                // if you get here you have connected to the WiFi
                // Serial.println("PARAM deviceid = " + getParam("deviceid"));
                // Serial.println("PARAM server = " + getParam("server"));
                Serial.println("connected...yeey :)");
            }
        }
    }
}

String getParam(String name)
{
    // read parameter from server, for customhmtl input
    String value;
    if (wm.server->hasArg(name))
    {
        value = wm.server->arg(name);
    }
    return value;
}

void saveParamCallback()
{
    Serial.println("[CALLBACK] saveParamCallback fired");
    shouldSaveConfig = true;
}

void updateSerial()
{
    delay(500);
    while (Serial.available())
    {
        Serial2.write(Serial.read()); // Forward what Serial received to Software Serial Port
    }
    while (Serial2.available())
    {
        Serial.write(Serial2.read()); // Forward what Software Serial received to Serial Port
    }
}

void displayInfo()
{
    Serial.print(F("Location: "));
    if (gps.location.isValid())
    {
        Serial.print(gps.location.lat(), 6);
        Serial.print(F(","));
        Serial.print(gps.location.lng(), 6);
    }
    else
    {
        Serial.print(F("INVALID"));
    }
}

void loop()
{
    int packetSize = LoRa.parsePacket(); // try to parse packet
    socketIO.loop();
    if (packetSize)
    {

        Serial.print("Received packet '");

        while (LoRa.available()) // read packet
        {
            String LoRaData = LoRa.readString(); // lon: value, lat: value
            Serial.print(LoRaData);
            // Parsing lon
            int lonStart = LoRaData.indexOf("lon: ") + 5; // 5 is the length of "lon: "
            int lonEnd = LoRaData.indexOf(',', lonStart);
            String lon = LoRaData.substring(lonStart, lonEnd);

            // Parsing lat
            int latStart = LoRaData.indexOf("lat: ") + 5; // 5 is the length of "lat: "
            int latEnd = LoRaData.length();
            String lat = LoRaData.substring(latStart, latEnd);

            if (socketIO.isConnected())
            {
                // delay(1000);
                // // creat JSON message for Socket.IO (event)
                DynamicJsonDocument doc(1024);
                JsonArray array = doc.to<JsonArray>();

                // // add evnet name
                // // Hint: socket.on('event_name', ....
                array.add("iotdata");

                // // add payload (parameters) for the event
                JsonObject param1 = array.createNestedObject();
                param1["lon"] = lon;
                param1["lat"] = lat;

                // // JSON to String (serializion)
                String output;
                serializeJson(doc, output);

                // // Send event
                socketIO.sendEVENT(output);

                // Print JSON for debugging
                // Serial.println(output);
            }
        }
        Serial.print("' with RSSI "); // print RSSI of packet
        Serial.println(LoRa.packetRssi());
    }

    if (wm_nonblocking)
        wm.process(); // avoid delays() in loop when non-blocking and other long running code
    checkButton();

    // updateSerial();
    // while (Serial2.available() > 0)
    //     if (gps.encode(Serial2.read()))
    //         displayInfo();

    // if (millis() > 5000 && gps.charsProcessed() < 10)
    // {
    //     Serial.println(F("No GPS detected: check wiring."));
    //     while (true)
    //         ;
    // }

    // socketIO.loop();

    // if (socketIO.isConnected())
    // {
    //     // // Clears the trigPin
    //     // digitalWrite(trigPin, LOW);
    //     // delayMicroseconds(2);
    //     // // Sets the trigPin on HIGH state for 10 micro seconds
    //     // digitalWrite(trigPin, HIGH);
    //     // delayMicroseconds(10);
    //     // digitalWrite(trigPin, LOW);

    //     // // Reads the echoPin, returns the sound wave travel time in microseconds
    //     // duration = pulseIn(echoPin, HIGH);

    //     // // Calculate the distance
    //     // distanceCm = duration * SOUND_SPEED / 2;

    //     // // Convert to inches
    //     // distanceInch = distanceCm * CM_TO_INCH;

    //     // photoresistorvalue = analogRead(photoPin);
    //     // // Serial.print("Photoresistor value: ");
    //     // // Serial.println(photoresistorvalue);
    //     // ambianttemp = mlx.readAmbientTempC();
    //     // // Serial.print("Ambiant temperature: ");
    //     // // Serial.println(ambianttemp);
    //     // objecttemp = mlx.readObjectTempC();
    //     // // Serial.print("Object temperature: ");
    //     // // Serial.println(objecttemp);

    //     // bool led1PinStatus = digitalRead(led1Pin);
    //     // bool led2PinStatus = digitalRead(led2Pin);
    //     // bool led3PinStatus = digitalRead(led3Pin);
    //     // bool led4PinStatus = digitalRead(led4Pin);

    //     // // read humidity
    //     // float humi = dht_sensor.readHumidity();
    //     // // read temperature in Celsius
    //     // float tempC = dht_sensor.readTemperature();
    //     // // read temperature in Fahrenheit
    //     // float tempF = dht_sensor.readTemperature(true);

    //     // // check whether the reading is successful or not
    //     // if (isnan(tempC) || isnan(tempF) || isnan(humi))
    //     // {
    //     //   Serial.println("Failed to read from DHT sensor!");
    //     // }
    //     // else
    //     // {
    //     //   Serial.print("Humidity: ");
    //     //   Serial.print(humi);
    //     //   Serial.print("%");

    //     //   Serial.print("  |  ");

    //     //   Serial.print("Temperature: ");
    //     //   Serial.print(tempC);
    //     //   Serial.print("°C  ~  ");
    //     //   Serial.print(tempF);
    //     //   Serial.println("°F");
    //     // }

    //     delay(1000);

    //     // // creat JSON message for Socket.IO (event)
    //     // DynamicJsonDocument doc(1024);
    //     // JsonArray array = doc.to<JsonArray>();

    //     // // add evnet name
    //     // // Hint: socket.on('event_name', ....
    //     // array.add("iotdata");

    //     // // add payload (parameters) for the event
    //     // JsonObject param1 = array.createNestedObject();
    //     // param1["pr"] = (uint32_t)photoresistorvalue;
    //     // param1["at"] = ambianttemp;
    //     // param1["ot"] = objecttemp;
    //     // param1["uss"] = distanceCm;
    //     // param1["humi"] = humi;
    //     // param1["tempC"] = tempC;
    //     // param1["tempF"] = tempF;
    //     // param1["led1"] = led1PinStatus;
    //     // param1["led2"] = led2PinStatus;
    //     // param1["led3"] = led3PinStatus;
    //     // param1["led4"] = led4PinStatus;

    //     // // JSON to String (serializion)
    //     // String output;
    //     // serializeJson(doc, output);

    //     // // Send event
    //     // socketIO.sendEVENT(output);

    //     // Print JSON for debugging
    //     // Serial.println(output);
    // }
}