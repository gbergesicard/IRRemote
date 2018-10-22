#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h> 
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <string>
#include <DNSServer.h>
#include <IRsend.h>
#include <FS.h>                 // File system management
#define REV "REV0001"


const char *ssidAP = "ConfigureDevice"; //Ap SSID
const char *passwordAP = "";  //Ap Password
char revision[10]; //Read revision 
char ssid[33];     //Read SSID From Web Page
char pass[64];     //Read Password From Web Page
char mqtt[100];    //Read mqtt server From Web Page
char mqttport[6];  //Read mqtt port From Web Page
char idx[10];      //Read idx From Web Page
char timer[10];    //Read timer value From Web Page
int startServer = 0;
int debug = 1;
int button = 0;
int timerMillisEnd = 0;
int timerKeepAliveMqtt = 0; //60 sec
int delayKeepAlive = 160;
char delayMessage[20];
uint16_t codeList[200];

ESP8266WebServer server(80);//Specify port 
WiFiClient ESPclient;
#define IR_LED D2  // ESP8266 GPIO pin to use. Recommended: 4 (D2).
#define BUTTON_PIN D6
#define LEDPIN D5
IRsend irsend(IR_LED);  // Set the GPIO to be used to sending the message.
File fsUploadFile;
char meta[] = "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
const String csvFileName="/IRRemote.csv";
bool csvFileExists = false;
const String cssFileName="/IRRemote.css";
String css ="";
// for the captive network
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
int captiveNetwork = 0;
bool mqttKO = false;
uint16_t LED_ON[71] = {9084, 4486,  598, 536,  596, 534,  598, 536,  596, 536,  596, 538,  596, 536,  598, 536,  596, 536,  598, 1628,  610, 1642,  598, 1642,  596, 1642,  598, 538,  596, 1628,  610, 1642,  598, 1640,  600, 1640,  600, 1640,  600, 536,  596, 534,  598, 536,  598, 536,  598, 536,  596, 540,  596, 534,  600, 538,  594, 1642,  598, 1644,  596, 1640,  598, 1642,  598, 1640,  598, 1642,  598, 41030,  9074, 2218,  600};  // NEC F7C03F

// for mqtt
int networkConnected = 0;
PubSubClient client(ESPclient);

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)
void handleFileUpload();                // upload a new file to the SPIFFS

////////////////////////////////////////////////////////////////////////////////////////////////
// Trace management
////////////////////////////////////////////////////////////////////////////////////////////////
void traceChln(char* chTrace){
  if (debug == 1){
    Serial.println(chTrace);
  }
}
void traceChln(String chTrace){
  if (debug == 1){
    Serial.println(chTrace);
  }
}
void traceCh(char* chTrace){
  if (debug == 1){
    Serial.print(chTrace);
  }
}
void traceCh(String chTrace){
  if (debug == 1){
    Serial.print(chTrace);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Timer management
////////////////////////////////////////////////////////////////////////////////////////////////
// Set the timer for a given number of minutes
void setTimer(int minutes){
  if (minutes <=0){
    timerMillisEnd = 0;
  }
  traceCh("Set timer for (min)");
  traceChln(String(minutes));
  

  timerMillisEnd = millis() + minutes*60000;
}

// Check the status of the timer return 1 when the timer end
int checkTimer(){
  if (timerMillisEnd == 0){
    return (0);
  }
  if (millis() >= timerMillisEnd){
    traceChln("Timer end");
    // reset the timer
    timerMillisEnd = 0;
    setParams(ssid,pass,mqtt,mqttport,idx,"0");
    return(1);
  }
  return (0);
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Mqtt code management
////////////////////////////////////////////////////////////////////////////////////////////////
void dumpCodeList(uint16_t* codeArray,int len){
  if( debug == 0){
    return;
  }
  traceCh("dumpCodeList len = "+String(len)+" code = (");
  for(int wni=0;wni<len;wni++){
    traceCh(String(codeArray[wni])+",");
  }
  traceChln(")");
}
void processIRCode(int code){
  switch ( code )  
  {  
     case 101:  
        // Tempo 1 min
        setTimer(1);
        break;  
     case 130:  
        // Tempo 30 min
        setTimer(30);
        break;  
     case 160:  
        // Tempo 60 min
        setTimer(60);
        break;  
     case 220:  
        // Tempo 120 min
        setTimer(120);
        break;  
     case 280:  
        // Tempo 180 min
        setTimer(180);
        break;  
     default:
        String desc ="";
        int type = 0;
        int len = 0;
        traceChln("Code from MQTT : "+String(code));
        if (getIRCodeInCSV(csvFileName, code, &desc, &type, &len, codeList) == -1){
          return;
        }
        else{
          traceChln("Code from MQTT : "+String(code)+" desc "+ desc);
          
          dumpCodeList(codeList,len);
          irsend.sendRaw(codeList, len, 38);
          //irsend.sendRaw(LED_ON, len, 38);
        }
  }
  onOffLed();
  return;  
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Setting management
////////////////////////////////////////////////////////////////////////////////////////////////
void dumpEEPROM(){
  char output[10];
  traceChln("dumpEEPROM start");
  for(int i =0;i<70;i++){
    snprintf(output,10,"%x ",EEPROM.read(i));
    traceCh(output);
  }
  traceChln("\ndumpEEPROM stop");
}

// Clear Eeprom
void ClearEeprom(){
  traceChln("Clearing Eeprom");
  for (int i = 0; i < 500; ++i) { EEPROM.write(i, 0); }
  traceChln("Clearing Eeprom end");
}

void readParam(char* value,int* index){
  char currentChar = '0';
  int i = 0;
  value[0]='\0';
  do{
    currentChar = char(EEPROM.read(*index));
    if(currentChar != '\0'){
      value[i] = currentChar;
    }
    (*index)++;
    i++;
  }while (currentChar != '\0');
  value[i]='\0';
  traceCh("readParam (");
  traceCh(value);
  traceChln(")");
}
int readRevison(char* value,int* index){
  char currentChar = '0';
  int i = 0;
  value[0]= '\0';
  do{
    currentChar = char(EEPROM.read(*index));
    if(currentChar != '\0'){
      value[i] = currentChar;
    }
    if(*index == 2 && !(value[0] =='R' && value[1] =='E' && value[2] =='V')){
      traceCh("Revision read : ");
      traceChln(value);
       return -1;
    }
    (*index)++;
    i++;
  }while (currentChar != '\0');
  value[i]='\0';
  traceCh("Revision read : ");
  traceChln(value);

  // Compare the revision
  if (strcmp(value,REV) != 0){
    return -2;
  }
  return 0;
}

int getParams(char* revision,char* ssid,char* pass,char* mqtt,char* mqttPort,char* idx,char* timer){
  int i =0;
  int wnReturn = 0;
  wnReturn = readRevison (revision,&i);
  traceCh("getParams wnReturn = ");
  traceChln(String(wnReturn));
  
  if (wnReturn <0){
    return -1;
  }

  readParam(ssid,&i);
  readParam(pass,&i);
  readParam(mqtt,&i);
  readParam(mqttport,&i);
  readParam(idx,&i);
  readParam(timer,&i);
  return 0;
}

void writeParam(char* value,int* index){
  traceCh("writeParam ");
  traceCh(value);
  traceCh(" Index = ");
  traceChln(String(*index));
  for (int i = 0; i < strlen(value); ++i)
  {
    EEPROM.write(*index, value[i]);
    (*index) ++;
  }
  EEPROM.write(*index, '\0');
  (*index)++;
}
void setParams(char* ssid,char* pass,char* mqtt,char* mqttPort,char* idx,char* timer){
  int i =0;
  
  ClearEeprom();//First Clear Eeprom
  delay(10);
  writeParam(REV,&i);
  writeParam(ssid,&i);
  writeParam(pass,&i);
  writeParam(mqtt,&i);
  writeParam(mqttPort,&i);
  writeParam(idx,&i);
  writeParam(timer,&i);
  EEPROM.commit();
}
void resetSettings(){
  traceChln("");
  traceChln("Reset EEPROM");  
  traceChln("Rebooting ESP");
  ClearEeprom();//First Clear Eeprom
  server.send(200, "text/plain", "Reseting settings, ESP will reboot soon");
  delay(100);
  EEPROM.commit();
  ESP.restart();
}

////////////////////////////////////////////////////////////////////////////////////////////////
// MQTT management
////////////////////////////////////////////////////////////////////////////////////////////////

void parseMqttMessage(char* message,int* index,int* value){
  String strMessage(message);
  int posIdx = 0;
  int posValue = 0;
  int posComma;
  String strTempo;

  traceChln(strMessage);
  posIdx = strMessage.indexOf("idx");
  if(posIdx<0){
    *index = 0;
    *value = 0;
    return;
  }
  posValue = strMessage.indexOf("nvalue");
  if(posValue<0){
    *index = 0;
    *value = 0;
    return;
  }
  strTempo = strMessage.substring(posIdx);
  posComma = strTempo.indexOf(',');
  strTempo = strTempo.substring(0,posComma);
  traceChln("strTempo : "+strTempo);
  strTempo.trim();
  *index = strTempo.toInt();

  strTempo = strMessage.substring(posValue);
  posComma = strTempo.indexOf(',');
  strTempo = strTempo.substring(0,posComma);
    if (debug == 1){
    Serial.println("strTempo : "+strTempo);
  }
  strTempo.trim();
  *value = strTempo.toInt();

}

void callback(char* topic, byte* payload, unsigned int length) {
  int indexParsed = 0;
  int valueParsed = 0;
  char tempo[10];

  tempo[0] = '\0';
  //parseMqttMessage((char*)payload,&indexParsed,&valueParsed);
  if (debug == 1){
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] (");
    for (int i = 0; i < length; i++) {
      Serial.print((char)payload[i]);
    }
    Serial.println(")");
  }
  for(int i = 0;i<length; i++){
    tempo[i] = payload[i];
  }
  tempo[length] = '\0';

  // Process IR code
  traceCh("tempo = ");
  traceChln(tempo);
  processIRCode(atoi(tempo));
  
}

void reconnect() {
  int wni =0;
  traceChln("reconnect Enter");
  // Loop until we're reconnected
  if (WiFi.status() != WL_CONNECTED) {
    char chTimer[30];
    int remainingTimer;
    remainingTimer = timerMillisEnd - millis();
    chTimer[0]='\0';
    sprintf(chTimer,"%d",remainingTimer);
    traceChln("Save timer");
    setParams(ssid,pass,mqtt,mqttport,idx,chTimer);
    traceChln("Rebooting ESP");
    ESP.restart();
  }
  traceChln("reconnect before while mqtt client status : "+String(client.connected()));
  while (!client.connected()) {
    // hangle web server requests 
    server.handleClient();
    traceCh("Attempting MQTT connection to server ");
    traceCh(mqtt);
    traceCh("...");
    manageButton();
    if(checkTimer() == 1){
      // send the IR_OFF
      // $*$*$ irsend.sendRaw(LED_OFF, 71, 38);
    }
    String clientId = idx;
   
    // Attempt to connect
    if (client.connect(clientId.c_str(),"uxewingr","up2OdBVGET64")) {
      traceChln("connected");
      // Once connected, publish an announcement...
      client.publish("led/out", "IR connected");
      // ... and resubscribe
      client.subscribe("led/in");
    } else {
      if(wni<5)
      {
        wni++;
        traceCh("failed, rc=");
        traceChln(String(client.state()));
        traceChln(" try again in 1 seconds");
        // Wait 1 second before retrying
      delay(1000);
      }
      else{
        mqttKO = true;
        traceChln("Mqtt connection not possible, abort");
        return;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Web pages
////////////////////////////////////////////////////////////////////////////////////////////////

// Generate the server page
void network_Page() {
  int Tnetwork=0,i=0,len=0;
  String st="",s="";
  Tnetwork = WiFi.scanNetworks(); // Scan for total networks available
  st = "<select name='ssid'>";
  for (int i = 0; i < Tnetwork; ++i)
  {
    // Add ssid to the combobox
    st += "<option value='"+WiFi.SSID(i)+"'>"+WiFi.SSID(i)+"</option>";
  }
  st += "</select>";
  IPAddress ip = WiFi.softAPIP(); // Get ESP8266 IP Adress
  // Generate the html setting page
  header(&s);
  s +="<h1>Device settings</h1> ";
  s += "<p>";
  s += "<form method='get' action='a'>"+st+"<label>Paswoord: </label><input name='pass' length=64><br><label>MQTT Server : </label><input name='mqtt' length=64 value="+mqtt+"><br><label>MQTT port : </label><input name='mqttPort' length=6 value="+mqttport+"><br><label>Idx: </label><input name='idx' length=6 value="+idx+"><br><input type='submit'></form>";
  footer(&s);
  server.send( 200 , "text/html", s);
}
// Process reply 
void Get_Req(){
  if (server.hasArg("ssid") && server.hasArg("pass")){  
    strcpy(ssid,server.arg("ssid").c_str());//Get SSID
    strcpy(pass,server.arg("pass").c_str());//Get Password
    strcpy(mqtt,server.arg("mqtt").c_str());
    strcpy(mqttport,server.arg("mqttPort").c_str());
    strcpy(idx,server.arg("idx").c_str());
  }
  // Write parameters in eeprom
  setParams(ssid,pass,mqtt,mqttport,idx,"0");
  String s = "";
  header(&s);
  s += "<h1>Device settings</h1> ";
  s += "<p>Settings Saved... Reset to boot into new wifi";
  footer(&s);
  server.send(200,"text/html",s);
  traceChln("Rebooting ESP");
  ESP.restart();
}

////////////////////////////////////////////////////////////
void header(String* s){
  *s = "<!DOCTYPE html>";
  *s += meta;
  *s += "<html>";
  *s += "<head><style>"+css+"</style><title>IR Remote</title></head>"; 
  *s += "<body>";
}
void footer(String* s){
  *s +="<br><br><a href=\"/\">Home</a><br>";
  *s += "</body>";
  *s += "</html>";
}
void root_Page() {
  traceChln("Serving Root Page");
  String s="";
  // Generate the html root page
  header(&s);
  s +="<h1>IR Remote</h1> ";
  s +="<a href=\"/settings\">Settings</a><br>";
  s +="<a href=\"/ircodelist\">Code</a><br>";
  s += "</body>";
  s += "</html>";
  server.send( 200 , "text/html", s);
}
void settings_Page() {
  traceChln("Serving settings Page");
  String s="";
  // Generate the html root page
  header(&s);
  s +="<h1>IR Remote Settings</h1> ";
  s +="<a href=\"/up\">Upload file</a><br>";
  s +="<a href=\"/network\">Network</a><br>";
  s +="<a href=\"/ircodelist\">Network</a><br>";
  footer(&s);
  server.send( 200 , "text/html", s);
}

void ircode_Page() {
  traceChln("Serving travel Page");
//  traceCh("Nb IR Codes :");
//  traceChln(String(arraySize));
  String s = "";
  // Generate the html root page
  header(&s);
  s += "<h1>IR Code List</h1> ";
  s += "<table>";
//  s += "<h2>Total nimber of IR Codes : " + String(arraySize) + "</h2>";
  s += "<tr>";
  s += "  <th>Description</th>";
  s += "  <th>Mqtt code</th>";
  s += "  <th>Type</th>";
  s += "  <th>Length</th>";
  s += "</tr>";
  // loop on the liste elements
  String desc ="";
  int type = 0;
  int len = 0;
  short wni = 0;
  while(getIRCodeInCSV(csvFileName, wni, &desc, &type, &len, codeList) >0){
    s += "<tr>";
    s += "<td>";
    s += String(desc);
    s += "</td>";
    s += "<td>";
    s += String(wni);
    s += "</td>";
    s += "<td>";
    s += String(type);
    s += "</td>";
    s += "<td>";
    s += String(len);
    s += "</td>";
    s += "</tr>";
    wni++;
  }
  s += "</table>";
  footer(&s);
  server.send( 200 , "text/html", s);
}

void upload_Page(){
  traceChln("Serving Upload Page");
  String s="";
  // build page
  header(&s);
  s += "<h1>IR Remote File Upload</h1>";
  s += "<form action=\"/upload\" method=\"post\" enctype=\"multipart/form-data\">";
  s += "    <input type=\"file\" name=\"name\">";
  s += "    <input class=\"button\" type=\"submit\" value=\"Upload\">";
  s += "</form>";
  footer(&s);
  // Send page
  server.send( 200 , "text/html", s);
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Utilities
/////////////////////////////////////////////////////////////////
void blinkLed(void){
  digitalWrite(LEDPIN, LOW);
  delay(200);
  digitalWrite(LEDPIN, HIGH);
  delay(200);
  digitalWrite(LEDPIN, LOW);
  delay(200);
  digitalWrite(LEDPIN, HIGH);
  delay(200);
  digitalWrite(LEDPIN, LOW);
  delay(200);
  digitalWrite(LEDPIN, HIGH);
  delay(200);
  digitalWrite(LEDPIN, LOW);
}

void onOffLed(void){
  digitalWrite(LEDPIN, LOW);
  digitalWrite(LEDPIN, HIGH);
  delay(200);
  digitalWrite(LEDPIN, LOW);
}

void manageButton(void){
  button = digitalRead(BUTTON_PIN);
  if (button == LOW){
    traceChln(" Button pressed");
    digitalWrite(LEDPIN, HIGH);
    delay(1000);
    button = digitalRead(BUTTON_PIN);
    if (button == LOW){
      blinkLed();
      ssid[0]='\0';
      setParams(ssid,pass,mqtt,mqttport,idx,"0");
      traceChln("Rebooting ESP");
      ESP.restart();
    }
  }
  else{
    digitalWrite(LEDPIN, LOW);
  }
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  traceChln("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    traceChln(String("\tSent file: ") + path);
    return true;
  }
  traceChln(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleFileUpload() { // upload a new file to the SPIFFS
  traceChln("uploading a file !");
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    traceChln("handleFileUpload Name: "); traceChln(filename);
    fsUploadFile = SPIFFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                                   // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      traceChln("handleFileUpload Size: "); traceChln(String(upload.totalSize));
      traceChln("file name : " + upload.filename);
      if (("/"+upload.filename)==csvFileName) {
        csvFileExists = true;        
      }
      if (("/"+upload.filename)==cssFileName) {
        updateCss(cssFileName);        
      }
      server.sendHeader("Location", "/success.html");     // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}


void updateCss(String fileName){
    traceChln("updateCss load file");
    traceChln(fileName);
  if(fileName == ""){
    return;
  }
  File dataFile = SPIFFS.open(fileName, "r");   //open file (path has been set elsewhere and works)
  css = dataFile.readString();                  // read data to 'css' variable
  dataFile.close();                             // close file
  traceChln(css);
}

bool isFileExists(String filename){
  return(SPIFFS.exists(filename));
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Csv File management
// Thgis function will be able to extract a ligne depending on the mqtt code and it will extract
// the different values.
////////////////////////////////////////////////////////////////////////////////////////////////
int getIRCodeInCSV(String fileName,int mqttCode,String* desc,int* type, int* len,uint16_t* codeArray){
  traceCh("getIRCodeInCSV mqttCode = ");
  traceChln(String(mqttCode));
  if (csvFileExists == true){
    File dataFile = SPIFFS.open(fileName, "r");
    int wni = 0;
    String curLine = "";
    String codes = "";
    while(wni<=mqttCode){
      curLine = dataFile.readStringUntil('\r');
      //traceChln("getIRCodeInCSV line read = " + curLine);
      wni++;
    }
    traceCh("getIRCodeInCSV wni = ");
    traceChln(String(wni));
    if(wni == mqttCode+1){
      //traceCh("getIRCodeInCSV line to process : ");
      //traceChln(curLine);
      if (curLine == ""){
        traceChln("getIRCodeInCSV line is empty");
        return -1;
      }
      // extract the data from the curLine
      int from = -1;
      int to = -1;
      to = curLine.indexOf(';');
      if (to < 0){
        traceChln("getIRCodeInCSV line not properly formated");
        return -1;
      }
      *desc = curLine.substring(0,to);
      from = to;
      to = curLine.indexOf(';',to+1);
      *type = atoi(curLine.substring(from+1,to).c_str());
      from = to;
      to = curLine.indexOf(';',to+1);
      *len = atoi(curLine.substring(from+1,to).c_str());
      traceChln("getIRCodeInCSV len = "+String(*len));
      from = to;
      codes = curLine.substring(from+1,curLine.length());
      // convert string codes in int array
      if(convertStringToArray(codes,codeArray) == *len){
        traceChln("getIRCodeInCSV OK");
        return 1; // OK !
      }
    }
    else{
      traceChln("getIRCodeInCSV KO1");
      return -1;
    }
  }
  else{
    traceChln("getIRCodeInCSV No CSV file");
  }
  traceChln("getIRCodeInCSV KO2");
  return -1;
}
// Will return the number of converted items otherwise -1
int convertStringToArray(String codes, uint16_t* codeArray){
  int from = -1;
  int to = -1;
  int wni = 0;
  traceChln(codes);
  to = codes.indexOf(',');
  if (to < 0){
    traceChln("convertStringToArray code not properly formated");
    return -1;
  }
  while(to > 0){
    codeArray[wni] = atoi(codes.substring(from+1,to).c_str());
    //traceChln("from "+String(from+1)+" to "+to);
    //traceChln(String(atoi(codes.substring(from+1,to).c_str())));
    from = to;
    to = codes.indexOf(',',to+1);
    wni++;
  }
  //extract the last item
  //traceChln(codes.substring(from+1).c_str());
  //traceCh("convertStringToArray last item : ");
  //traceChln(codes.substring(from+1, codes.length()).c_str());
  codeArray[wni] = atoi(codes.substring(from+1, codes.length()).c_str());
  wni++;
  traceCh("convertStringToArray wni = ");
  traceChln(String(wni));
  return wni;
}

void setup() {
  //pinMode(0, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LEDPIN, OUTPUT);
  irsend.begin();
  startServer = 1;
  revision[0] = '\0';
  ssid[0] = '\0';
  pass[0] = '\0';
  mqtt[0] = '\0';
  mqttport[0] = '\0';
  idx[0] = '\0';
  timer[0] = '\0';
  delayMessage[0] = '\0';

  // initilize file system
  SPIFFS.begin();
  Serial.begin(115200); //Set Baud Rate 
  EEPROM.begin(512);
  traceChln("Configuring access point...");
  dumpEEPROM();
  // when needed 
  //resetSettings();
  // Load csv file
  if (isFileExists(csvFileName)){
    traceChln("Csv file OK");
    csvFileExists = true;
  }
  else{
    traceCh("File doesn't exist : ");
    traceChln(csvFileName);
  }
  if (isFileExists(cssFileName)){
    updateCss(cssFileName);
  }
  else{
    traceCh("File doesn't exist : ");
    traceChln(cssFileName);
  }

  // Reading EEProm SSID-Password
  if(getParams(revision,ssid,pass,mqtt,mqttport,idx,timer) !=0 ){
    // Config mode
    startServer = 0;
  }
  else{
    if (strcmp(timer,"0")!=0){
      traceChln("A timer needs to be set");
      timerMillisEnd = atoi(timer);
    }
    if(ssid[0] != '\0'){
      WiFi.mode(WIFI_STA);
      delay(200); //Stable Wifi
      traceChln(ssid);
      traceChln(pass);
      traceChln(mqtt);
      traceChln(mqttport);
      traceChln(idx);
      traceChln(timer);
      WiFi.begin(ssid,pass);
      delay(200); //Stable Wifi
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        traceChln(".");
        manageButton();
        if(checkTimer() == 1){
          // send the IR_OFF
        //$*$*$  irsend.sendRaw(LED_OFF, 71, 38);
        }
      }
      traceCh(".");
      traceChln("");
      traceChln("WiFi connected");
      traceChln("IP address: ");
      if (debug == 1){
        Serial.println(WiFi.localIP());
      }
      blinkLed();
      // Connect mqtt server
      client.setServer(mqtt, atoi(mqttport));
      client.setCallback(callback);
      networkConnected = 1;
      WiFi.hostname("SwitchWIFI");
    }
    else{
      startServer = 0;
    }
  }
  // Start the web server
  server.on("/",root_Page);
  server.on("/up",upload_Page);
  server.on("/settings",settings_Page);
  server.on("/reset",resetSettings);
  server.on("/network",network_Page); 
  server.on("/ircodelist",ircode_Page); 
  server.on("/a",Get_Req); // If submit button is pressed get the new SSID and Password and store it in EEPROM 
  server.on("/upload", HTTP_POST,                       // if the client posts to the upload page
    [](){ server.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
    handleFileUpload                                    // Receive and save the file
  );
  server.on("/upload", HTTP_GET, []() {                 // if the client requests the upload page
  if (!handleFileRead(csvFileName))                // send it if it exists
    server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });
  server.begin();
}

void loop() {
  manageButton();
  if (startServer == 0){ 
    traceChln("Starting Access point..");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Wifi device setup");
    delay(100); //Stable AP
 
    dnsServer.start(DNS_PORT, "*", apIP);
    startServer = 1;
    // captive network is active 
    captiveNetwork = 1;
    traceChln("Captive network activated");
  }
  delay(300); 
  if (captiveNetwork == 1){
    dnsServer.processNextRequest();
  }
  if(networkConnected == 1){
    //traceChln("loop Mqtt client status :"+String(client.connected()));
    if (!client.connected()) {
      if(mqttKO == false){
         reconnect();
      }
    }
    else{
      if (timerKeepAliveMqtt >= delayKeepAlive){
        // send the keepAlive
        traceChln("Keep alive message");
        client.publish("led/out", "Keep alive message");
        if(timerMillisEnd > 0){
          int remainingTime = timerMillisEnd - millis();
          sprintf(delayMessage,"Temps Restant %d sec", (int)(remainingTime/1000));
          client.publish("led/out", delayMessage);
        }
        timerKeepAliveMqtt = 0;
      }
      else{
        timerKeepAliveMqtt++;
      }
      client.loop();
    }
  }
  if(checkTimer() == 1){
    // send the IR_OFF
    //irsend.sendRaw(LED_OFF, 71, 38);
  }
  server.handleClient(); 
}
