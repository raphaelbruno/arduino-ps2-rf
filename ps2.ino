#include <PS2X_lib.h>
#include <RF24.h>


#define BAUD_RATE           115200
#define TRANSMITTER         true      // If true it is Transmitter, if false it is receiver
#define TIME_RESEND         1000      // Time to force resend in milliseconds
#define TIME_RESET_CHANNEL  3000      // Time to force reset channel on hold select button in milliseconds
#define RECEIVER_CHANNEL    1         // Channel of the receiver [1~10]
#define RECEIVER_TIMEOUT    5000      // Receiver timeout to lost transmitter in milliseconds
#define DEBUG               true      // If true it will log at serial monitor

#define BUZZER              2         // Buzzer PIN

#define PS2_SEL             5         // Attention PIN (Yellow)
#define PS2_CMD             6         // Command PIN (Orange)
#define PS2_CLK             7         // Clock PIN (Blue)
#define PS2_DAT             8         // Data PIN (Brown)

#define RF24_CE             9         // CE PIN
#define RF24_CNS            10        // CNS PIN

struct Controller
{
  public:
    byte pads = 0b00000000;
    byte buttons = 0b00000000;
    byte analogLX;
    byte analogLY;
    byte analogRX;
    byte analogRY;

    bool equals(const Controller& rhs) const {
      return pads == rhs.pads
        && buttons == rhs.buttons
        && analogLX == rhs.analogLX
        && analogLY == rhs.analogLY
        && analogRX == rhs.analogRX
        && analogRY == rhs.analogRY;
    }
};

RF24 radio(RF24_CE, RF24_CNS);
PS2X ps2x;
Controller controller;

int error = 0;
byte type = 0;
byte vibrate = 0;
int channel;
uint8_t addresses[][6] = {
  "00001", "00002", "00003", "00004", "00005",
  "00006", "00007", "00008", "00009", "00010"
};

unsigned long now = millis();
unsigned long lastForcedMessage = millis();
bool connected = false;

#if TRANSMITTER
  unsigned long timePressedSelect = millis();
#else
  unsigned long lastTimeReceived;
#endif


/************************************************************
 * Base
 ************************************************************/

void setup(){
  #if DEBUG
    Serial.begin(BAUD_RATE);
  #endif
  
  pinMode(BUZZER, OUTPUT);
    
  #if TRANSMITTER
    delay(250); // added delay to give wireless ps2 module some time to startup, before configuring it
    channel = 0;
    setupJoypad();
    delay(TIME_RESEND);
    setupTransmitter();
  #else
    channel = RECEIVER_CHANNEL - 1;
    setupReceiver();
  #endif
}

void loop() {
  now = millis();
  #if TRANSMITTER
    transmitter();
  #else
    receiver();
  #endif
}


#if TRANSMITTER
  /************************************************************
   * Setup
   ************************************************************/
  void setupJoypad() {
    #if DEBUG
      printTitle("SETUP JOYPAD");
    #endif
    
    error = ps2x.config_gamepad(PS2_CLK, PS2_CMD, PS2_SEL, PS2_DAT, false, false);
    type = ps2x.readType(); 

    if(error == 0 && type != 2) { //DualShock Controller
      #if DEBUG
        Serial.println("DualShock Controller found ");
      #endif

      setConnected();
    } else { // Not Found
      tone(BUZZER, 50, 500);
    }
  }

  void setupTransmitter(){
    #if DEBUG
      printTitle("SETUP TRANSMITTER");
      Serial.print("[CHANNEL:");
      Serial.print(channel+1);
      Serial.println("] ");
    #endif

    radio.begin();
    radio.openWritingPipe(addresses[channel]);
    radio.setPALevel(RF24_PA_MIN);
    radio.stopListening();
  }

  /************************************************************
   * Actions
   ************************************************************/
  void transmitter() {
    if(error == 1) return; 

    if(type != 2) { //DualShock Controller
      ps2x.read_gamepad(false, vibrate);

      Controller currentController;

      bitWrite(currentController.pads, 0, ps2x.Button(PSB_START));
      bitWrite(currentController.pads, 1, ps2x.Button(PSB_SELECT));
      bitWrite(currentController.pads, 2, ps2x.Button(PSB_PAD_UP));
      bitWrite(currentController.pads, 3, ps2x.Button(PSB_PAD_RIGHT));
      bitWrite(currentController.pads, 4, ps2x.Button(PSB_PAD_DOWN));
      bitWrite(currentController.pads, 5, ps2x.Button(PSB_PAD_LEFT));
      bitWrite(currentController.pads, 6, ps2x.Button(PSB_L3));
      bitWrite(currentController.pads, 7, ps2x.Button(PSB_R3));

      bitWrite(currentController.buttons, 0, ps2x.Button(PSB_TRIANGLE));
      bitWrite(currentController.buttons, 1, ps2x.Button(PSB_CIRCLE));
      bitWrite(currentController.buttons, 2, ps2x.Button(PSB_CROSS));
      bitWrite(currentController.buttons, 3, ps2x.Button(PSB_SQUARE));
      bitWrite(currentController.buttons, 4, ps2x.Button(PSB_L1));
      bitWrite(currentController.buttons, 5, ps2x.Button(PSB_R1));
      bitWrite(currentController.buttons, 6, ps2x.Button(PSB_L2));
      bitWrite(currentController.buttons, 7, ps2x.Button(PSB_R2));

      currentController.analogLX = ps2x.Analog(PSS_LX);
      currentController.analogLY = ps2x.Analog(PSS_LY);
      currentController.analogRX = ps2x.Analog(PSS_RX);
      currentController.analogRY = ps2x.Analog(PSS_RY);

      if(ps2x.ButtonPressed(PSB_SELECT))
        timePressedSelect = millis();

      if(ps2x.ButtonReleased(PSB_SELECT))
        changeChannel();

      if(now - lastForcedMessage >= TIME_RESEND || !controller.equals(currentController)){
        lastForcedMessage = now;
        controller = currentController;

        bool result = radio.write(&controller, sizeof(Controller));
        #if DEBUG
          Serial.print("[TRANSMITTED:");
          Serial.print(result);
          Serial.print("] ");
          printController(controller);
        #endif
      }
    }
  }

  void changeChannel() {
    if((now - timePressedSelect) >= TIME_RESET_CHANNEL){
      tone(BUZZER, 2000, 10);
      delay(50);
      tone(BUZZER, 2000, 10);
      delay(TIME_RESEND);
            
      channel = 0;
    }else{
      tone(BUZZER, 6000, 10);
      delay(200);
      
      int length = sizeof(addresses)/sizeof(addresses[0]);
      channel++;
      if(channel >= length) channel = 0;
    }

    setupTransmitter();
  }

#else
  /************************************************************
   * Setup
   ************************************************************/
  void setupReceiver(){
    #if DEBUG
      printTitle("SETUP RECEIVER");
      Serial.print("[CHANNEL:");
      Serial.print(channel+1);
      Serial.println("] ");
    #endif

    radio.begin();
    radio.openReadingPipe(0, addresses[channel]);
    radio.setPALevel(RF24_PA_MIN);
    radio.startListening();
  }


  /************************************************************
   * Actions
   ************************************************************/

  void receiver() {
    if(radio.available()){
      if(!connected) setConnected();
      lastTimeReceived = millis();
      
      radio.read(&controller, sizeof(Controller));
      #if DEBUG
        Serial.print("[RECEIVED] ");
        printController(controller);
      #endif

      // Serial.println(bitRead(controller.pads, 0)); // Start
      // Serial.println(bitRead(controller.pads, 1)); // Select
    }else{
      if(connected && (now - lastTimeReceived) >= RECEIVER_TIMEOUT)
        setDisconnected();
    }
  }
#endif

void setConnected() {
  #if DEBUG
    Serial.println("[CONNECTED]");
  #endif
  connected = true;
  tone(BUZZER, 2000, 100);
  delay(100);
  tone(BUZZER, 2500, 100);
  delay(100);
  tone(BUZZER, 3000, 100);
  delay(100);
}

void setDisconnected() {
  #if DEBUG
    Serial.println("[DISCONNECTED]");
  #endif
  connected = false;
  tone(BUZZER, 3000, 100);
  delay(100);
  tone(BUZZER, 2500, 100);
  delay(100);
  tone(BUZZER, 2000, 100);    
  delay(100);
}

/************************************************************
 * Print
 ************************************************************/
#if DEBUG
  void printTitle(char title[]) {
    Serial.println("-----------------------------------------");
    Serial.print("- ");
    Serial.println(title);
    Serial.println("-----------------------------------------");
  }

  void printController(Controller object) {
    Serial.print("Pads: ");
    Serial.print(object.pads);
    Serial.print(" Buttons: ");
    Serial.print(object.buttons);
    Serial.print(" Analog: L{");
    Serial.print(object.analogLX);
    Serial.print(",");
    Serial.print(object.analogLY);
    Serial.print("} R{");
    Serial.print(object.analogRX);
    Serial.print(",");
    Serial.print(object.analogRY);
    Serial.println("}");
  }
#endif
