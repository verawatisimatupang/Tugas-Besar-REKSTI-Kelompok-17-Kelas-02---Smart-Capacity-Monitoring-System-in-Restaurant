/* include library yang digunakan */
#include "esp_camera.h"
#include "time.h" 
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPDash.h>
#include "addons/TokenHelper.h" // Firebase library: Provide the token generation process info.
#include "addons/RTDBHelper.h" // Firebase library: Provide the RTDB payload printing info and other helper functions.
#define BUZZER_PIN 4 // GPIO4 for buzzer

/* define camera model*/
#define CAMERA_MODEL_AI_THINKER //CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

/* define pengaturan deteksi objek pada ESP camera */
#define FRAME_SIZE FRAMESIZE_QVGA // define the type of the frame size
#define WIDTH 320 //in pixels 
#define HEIGHT 240 //in pixels 
#define BLOCK_SIZE 10
#define W (WIDTH / BLOCK_SIZE) // in blocks 
#define H (HEIGHT / BLOCK_SIZE) //in blocks 
#define BLOCK_DIFF_THRESHOLD 0.2 // define the scaling factor  which means the percent threshold above which we say that the block is actually changed
#define IMAGE_DIFF_THRESHOLD 0.2 //define the number of blocks above which an image is changed 
#define DEBUG 1

/* define settings firebase */
#define API_KEY "AIzaSyAiAVSyALHFQddMgS_KUQ_kfeAvBbdzZY8" 
#define USER_EMAIL "abc@def.com"
#define USER_PASSWORD "abcdef"
#define DATABASE_URL "https://reksti-3b8e4-default-rtdb.asia-southeast1.firebasedatabase.app/"

/* define Firebase objects */
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

/* inisialisasi wifi credentials untuk web server */
const char* ssid = "CISITU LAMA LT2";
const char* password = "15juni1959";

/* inisialisasi time dari NTP Server*/
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;

/* variable to save USER UID */
String uid;

/* database main path (to be updated in setup with the user UID) */
String databasePath;

/* database child nodes */
String hariPath = "/1 hari";
String tanggalPath = "/2 tanggal";
String bulanPath = "/3 bulan";
String tahunPath = "/4 tahun";
String pengunjungPath = "/ 5 jumlah pelanggan";
String timePath = "/timestamp";

/* Parent Node (to be updated in every loop) */
String parentPath;

int timestamp;
FirebaseJson json;

// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 180000;

/* start webserver */
AsyncWebServer server(80);

/* attach ESP-DASH to AsyncWebServer */
ESPDash dashboard(&server); 

/* inisialisasi card untuk GUI webserver */
Card statusmeja1(&dashboard, STATUS_CARD, "Meja 1", "danger"); // format card ESP-DASH: Card nama(&dashboard, jenis_card, nama_card, satuan[optional]) 
Card statusmeja2(&dashboard, STATUS_CARD, "Meja 2", "danger"); // dokumentasi dilihat pada: docs.espdash.pro/cards/status/ 
Card jumlahorang(&dashboard, HUMIDITY_CARD, "Jumlah Pelanggan Sekarang");
Chart chart1(&dashboard, BAR_CHART, "Tren Pengunjung");

/* inisialisasi isi chart pengunjung */
String XAxis[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
int YAxis[] = {242, 21, 84, 43, 78, 103, 178};

/* inisialisasi frame object detection */
uint16_t prev_frame[H][W] = { 0 }; // the previous frame 
uint16_t current_frame[H][W] = { 0 };
int list[2]={0,0} ; // this list allows us to select only one direction when sereval directions are detected for just one person entering or getting out of the building 
int counter = -1 ;

/* iniasialiasai funtions kamera yang digunakan (daftar fungsi ada di bawah void loop) */
bool setup_camera(framesize_t);
bool capture_still();
int motion_detect();
void update_frame();
void print_frame(uint16_t frame[H][W]);
bool direction_detection ();
int freq(uint16_t frame[H][W],uint16_t a);

/* inisialisasi pin IO */
int trigPin1 = 13;
int echoPin1 = 15;
int trigPin2 = 2;
int echoPin2 = 14;

/* inisialisasi variabel waktu dan pengunjung*/
int pengunjungharian = 0 ;
String hari = "hari";
int tanggal = 0 ;
int bulan = 0 ;
int tahun = 0 ; 

void setup() {
    /* setup pin dan serial */
    Serial.begin(115200);
    Serial.println(setup_camera(FRAME_SIZE) ? "OK" : "ERR INIT");
    pinMode(12, OUTPUT);  // initialize the GPIO12 pin as an output
    pinMode(16, OUTPUT);  // initialize the GPIO16 pin as an output
    pinMode(trigPin1, OUTPUT);
    pinMode(echoPin1, INPUT);
    pinMode(trigPin2, OUTPUT);
    pinMode(echoPin2, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    /* connect ke Wi-Fi dengan SSID dan password */
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    
    /* print local IP address dan start web server */
    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    /* init and get the time */
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
    /* start AsyncWebServer */
    server.begin();

    /* assign firebase: api key (required), user sign, database url */
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.database_url = DATABASE_URL;
    Firebase.reconnectWiFi(true);
    fbdo.setResponseSize(4096);

    /* configuration object */
    config.token_status_callback = tokenStatusCallback;/* assign the callback function for the long running token generation task */ //see addons/TokenHelper.h
    config.max_token_generation_retry = 5; /* assign the maximum retry of token generation */

    /* initialize the library with the Firebase authen and config */
    Firebase.begin(&config, &auth);

    /* getting the user UID might take a few seconds */
    Serial.println("Getting User UID");
    while ((auth.token.uid) == "") {
      Serial.print('.');
      delay(1000);
    }
    /* print user UID */
    uid = auth.token.uid.c_str();
    Serial.print("User UID: ");
    Serial.println(uid);

    /* update database path */
    databasePath = "/UsersData/" + uid + "/readings";
}
 
void loop() {
  
    /* mendapatkan timeinfo dari NTP Server */
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      Serial.println("Failed to obtain time");
      return;
    }
    
    /* update data hari, tanggal, bulan, tahun */
    if (timeinfo.tm_wday == 0){hari = "Minggu";} // update hari
    else if (timeinfo.tm_wday == 1){hari = "Senin";}
    else if (timeinfo.tm_wday == 2){hari = "Selasa";}
    else if (timeinfo.tm_wday == 3){hari = "Rabu";}
    else if (timeinfo.tm_wday == 4){hari = "Kamis";}
    else if (timeinfo.tm_wday == 5){hari = "Jumat";}
    else if (timeinfo.tm_wday == 6){hari = "Sabtu";}
    tanggal = timeinfo.tm_mday; // update tanggal
    bulan = timeinfo.tm_mon + 1; // update bulan tambah 1 karna range bulan di NTP server 0-11
    tahun = timeinfo.tm_year + 1900; // update bulan tambah 1900 karna tahun di NTP server mulainya dari 1900   

    /* print tanggal hari ini*/
    Serial.print("hari: "); Serial.println(hari);
    Serial.print("tanggal: "); Serial.println(tanggal);
    Serial.print("bulan: "); Serial.println(bulan);
    Serial.print("tahun: "); Serial.println(tahun);
    Serial.print("jam: "); Serial.println(timeinfo.tm_hour); 
    Serial.print("menit: "); Serial.println(timeinfo.tm_min);   
    Serial.print("detik: "); Serial.println(timeinfo.tm_sec);     
    
    /* verifikasi tidak ada yang error saat deteksi gambar oleh kamera */
    if (!capture_still()) {
        Serial.println("Failed capture");      
        return;
    }
    
   /* menghitung jumlah orang dari kamera berdasarkan arah pergerakan */
   switch (motion_detect()){
    case 0 : 
      Serial.println("no motion");
      list[1]=0;
      digitalWrite(12, LOW);
      digitalWrite(16, LOW);
      break;  
    case 1 : 
      Serial.println("get in "); // when a person gets in that means that he is coming from the left side( the motion is detected from the left side of the photo) 
      list[1]= 1;
      digitalWrite(12, HIGH);
      digitalWrite(16, LOW);
      break; 
    case -1 : 
      Serial.println("get out"); // when a person gets out that means that he is coming from the right side( the motion is detected from the right side of the photo) 
      list[1]= -1 ;
      digitalWrite(12, LOW);
      digitalWrite(16, HIGH);
      break;       
   }
   
    /* perhitungan jumlah pengunjung (yang masuk dan keluar) */ 
    if ((list[0]==0) && (list[1]==1)){
       counter= counter+1 ; // pengunjung masuk ruangan, counter pengunjung terkini +1
       pengunjungharian = pengunjungharian + 1 ; //pengunjung harian bertambah
    }else if ((list[0]==0) && (list[1]==-1)){
        counter=counter-1 ; // pengunjung keluar ruangan, counter pengunjung terkini -1
       }
    if (counter < 0){ //error handling
      counter=0;   
    }

    /* update data pengunjung terkini */
    Serial.println("jumlah orang: "); Serial.println(counter);    
    jumlahorang.update(counter); 
    list[0]= list[1]; //the current value will be the preview value
    update_frame();
    
    /* pengambilan data dan perhitungan jarak sensor 1 */
    long duration1, distance1, duration2, distance2;
    digitalWrite(trigPin1,LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin1,HIGH);
    delayMicroseconds(2);
    digitalWrite(trigPin1, LOW);
    duration1=pulseIn(echoPin1, HIGH);
    distance1 =(duration1/2)/29.1;
    Serial.println("jarak sensor1: ");
    Serial.println(distance1);

    /* pengambilan data dan perhitungan jarak sensor 2 */
    digitalWrite(trigPin2,LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin2, HIGH);
    delayMicroseconds(2);
    digitalWrite(trigPin2,LOW);   
    duration2=pulseIn(echoPin2, HIGH);
    distance2 =(duration2/2)/29.1;
    Serial.println("jarak sensor2: ");
    Serial.println(distance2);

    /* perhitungan kapasitas tempat makan */
    if ((distance1 <= 10) && (distance2 <= 10)) {
      digitalWrite(BUZZER_PIN, HIGH);
      Serial.println("Meja 1 penuh");
      Serial.println("Meja 2 penuh");
      statusmeja1.update("Penuh", "danger");
      statusmeja2.update("Penuh", "danger");
    } else if ((distance1 <= 10) && (distance2 > 10)) {
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Meja 1 penuh");
      Serial.println("Meja 2 kosong");
      statusmeja1.update("Penuh", "danger");
      statusmeja2.update("Kosong", "success");
    } else if ((distance1 > 10) && (distance2 <= 10)) {
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Meja 1 kosong");
      Serial.println("Meja 2 penuh");
      statusmeja1.update("Kosong", "success");
      statusmeja2.update("Penuh", "danger");
    } else if ((distance1 > 10) && (distance2 > 10)) {
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("Meja 1 kosong");
      Serial.println("Meja 2 kosong");
      statusmeja1.update("Kosong", "success");
      statusmeja2.update("Kosong", "success");
    }
    
    /* update chart */
    chart1.updateX(XAxis, 7);
    chart1.updateY(YAxis, 7);

    /* send data statusmeja1, statusmeja2, jumlahorang, dan chart1 ke webserver secara realtime */
    dashboard.sendUpdates();
    
    /* send data pengunjungharian ke database tiap jam 23:59. // tm_hour == 17 mengikuti jam NTP. Jam indo adalah (jam NTP + 6 jam) sehingga 17+6 = jam 23 */
    if ((timeinfo.tm_hour == 17) && (timeinfo.tm_min == 59)){ /* sumber: cplusplus.com/reference/ctime/tm/ */
      Serial.println ("================Reset Pengunjung Harian================");  
      
      /* send data hari, tanggal, bulan, tahun, pengunjungharian ke firebase database */
      if (Firebase.ready()){
        timestamp = getTime(); // get current timestamp
        Serial.print ("time: "); Serial.println (timestamp); // print timestamp
        parentPath= databasePath + "/" + String(timestamp);  // update the parentPath variable to include the timestamp.

        /*add data to the json object by using the set() method and passing as first argument the child node destination (key) and as second argument the value */
        json.set(hariPath.c_str(), hari);
        json.set(tanggalPath.c_str(), String(tanggal));
        json.set(bulanPath.c_str(), String(bulan));
        json.set(tahunPath.c_str(), String(tahun));
        json.set(pengunjungPath.c_str(), String(pengunjungharian));      
        json.set(timePath, String(timestamp));

        /* call Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) to append the data to the parent path */
        /* we can call that instruction inside a Serial.printf() command to print the results in the Serial Monitor at the same time the command runs. */
        Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
      }
      
      /* reset pengunjung harian */
      pengunjungharian = 0;
      delay(60000); // note: delay 60 detik for sure the data send only once pada 23:59. SELURUH sistem akan delay selama 60 detik termasuk serial monitor 
    }

    /* pembatas print per looping */
    Serial.println("================================================================");
    delay(1000);           
}


//================================Fungsi-fungsi yang digunakan pada program================================//

unsigned long getTime() { /* gets current epoch time */
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}
bool setup_camera(framesize_t frameSize) { /* camera setup */
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = frameSize;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    
    bool ok = esp_camera_init(&config) == ESP_OK;

    sensor_t *sensor = esp_camera_sensor_get();
    sensor->set_framesize(sensor, frameSize);

    return ok;
}

bool capture_still() { /* capture image sampling per-block 10x10 pixel */
    camera_fb_t *frame_buffer = esp_camera_fb_get();

    if (!frame_buffer)
        return false;

    // set all 0s in current frame
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            current_frame[y][x] = 0;

    // down-sample image in blocks
    for (uint32_t i = 0; i < WIDTH * HEIGHT; i++) {
        const uint16_t x = i % WIDTH;
        const uint16_t y = floor(i / WIDTH);
        const uint8_t block_x = floor(x / BLOCK_SIZE);
        const uint8_t block_y = floor(y / BLOCK_SIZE);
        const uint8_t pixel = frame_buffer->buf[i];
        const uint16_t current = current_frame[block_y][block_x];
        
        // average pixels in block (accumulate)
        current_frame[block_y][block_x] += pixel;
    }
    // average pixels in block (rescale)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            current_frame[y][x] /= BLOCK_SIZE * BLOCK_SIZE;

    #if DEBUG
      //Serial.println("Current frame:");
      //print_frame(current_frame);
      //Serial.println("Preview frame:");
      //print_frame(prev_frame);
      //Serial.println("---------------");
    #endif
    return true;
}

bool direction_detection(uint16_t frame[H][W]){ /* menentukan arah pergerakan orang */
  // initialize the two direction matrices 
  uint16_t direc_left[H][W] = { 0 };
  uint16_t direc_right[H][W] = { 0 };
  
  for (int y = 0; y < H ; y++) {
      for (int x = 0; x < W; x++){
        if(x < W/2 ) {                   
          direc_left[y][x] = frame[y][x];
          direc_right[y][x] = { 0 };
        } 
        else {
           direc_right[y][x] = frame[y][x];
           direc_left[y][x] = { 0 };
        }
      } 
      }
      //print_frame(direc_left);
      //Serial.println("---------------");
      //print_frame(direc_right);
      Serial.println("....................");
  if (freq(direc_right,{99}) > freq(direc_left,{99})) {
    //Serial.println("Rechts Richtung");
    return false;
    } else if (freq(direc_right,{99}) < freq(direc_left,{99})) { 
      //Serial.println("Links Richtung");
      return true; 
    }
    //else {
      //Serial.println("error keine Richtung detektiert ");
    //}    
}

int motion_detect(){ /* motion detector dari block */
    uint16_t changes = {0}; //initialize the number of changed blocks 
    const uint16_t blocks = (WIDTH * HEIGHT) / (BLOCK_SIZE * BLOCK_SIZE); // set the number of blocks in the picture
    uint16_t direc[H][W] = { 0 };
    // set the direction matrix to all 0s
    for (int y = 0; y < H; y++)
       { for (int x = 0; x < W; x++)
       {direc[y][x] = {0};}}
    // compare the blocks of the current frame with the blocks of the previous frame and calculate the delta factor 
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float current = current_frame[y][x]; 
            float prev = prev_frame[y][x];
            float delta = abs(current - prev) / prev;
    
            if (delta >= BLOCK_DIFF_THRESHOLD) { 
    #if DEBUG
                  
                //Serial.print("diff\t");
                //Serial.print(y);
                //Serial.print('\t');
                //Serial.println(x);
                //delay(100);
                
                changes += 1;  //if the block has changed considerably then add one to the number of changed blocks 
                direc[y][x] = {99} ; // write 99 in the direction matrix refering to the changed block 
    #endif               
            }
        }
    }
     //Serial.print("Changed ");
     //Serial.print(changes);
     if ((1.0 * changes / blocks) > IMAGE_DIFF_THRESHOLD){
      if  (direction_detection(direc)) {
          return 1;  // 1 means there is motion from the leftside ==> someone is getting in
      } else {
        return -1 ; //-1 means there is motion from the rightside ==> someone is getting out  
      }  
    } else {
      return 0; // there is no motion 
    }
}

void update_frame() { /* copy current frame to previous */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            prev_frame[y][x] = current_frame[y][x];
        }
    }
}

int freq(uint16_t matrix[H][W],uint16_t a) { /* calculate the frequency of a number in a HxW matrix  */
   int freq = 0 ; 
   for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (matrix[y][x] == a) {
              freq = freq +1 ;
            }
        }
    }
    return freq ;
}

void print_frame(uint16_t frame[H][W]) { /* for serial debugging ürint the frame */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            Serial.print(frame[y][x]);
            Serial.print('\t');
        }
        Serial.println();
    }
}
 
