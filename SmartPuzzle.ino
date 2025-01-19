#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>

#define NUM_LEDS 36 // 6x6 matrix
#define DATA_PIN 21
#define BRIGHTNESS 5 // 5/255 brightness

CRGB leds[NUM_LEDS];

// Encoder pin definitions
#define ENCODER_A_PIN 10
#define ENCODER_B_PIN 9
#define BUTTON_PIN 0 // Updated button pin
#define NUM_COLORS 8

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_IP"; // Update with your MQTT broker IP address

WiFiClient espClient;
PubSubClient client(espClient);

// Color pool (8 colors)
CRGB colorPool[] = {CRGB::Red, CRGB::DarkOrange, CRGB::GreenYellow, CRGB::Teal, CRGB::Blue, CRGB::BlueViolet, CRGB::Purple, CRGB::DarkSalmon};
CRGB secretColors[3];
CRGB selectedColors[3];
CRGB gameBoard[6][6];

int currentLED = 0;
int currentColorIndex = 0;
int selectedColorCount = 0;
int currentRow = 0;

uint8_t buttonHistory = 0xFF; // Button history for debouncing

void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  pinMode(ENCODER_A_PIN, INPUT);
  pinMode(ENCODER_B_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  setup_wifi();
  client.setServer(mqtt_server, 1883);

  randomSeed(analogRead(1));
  for (int i = 0; i < 3; i++) {
    int randomIndex = random(0, NUM_COLORS);
    secretColors[i] = colorPool[randomIndex];
  }

  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 6; j++) {
      gameBoard[i][j] = CRGB::Black;
    }
  }
  gameBoard[0][0] = colorPool[0];
  FastLED.show();
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32Client")) {
      client.publish("home/puzzle_switch", "off");
    } else {
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  static uint8_t lastStateA = LOW;
  static uint8_t lastStateB = LOW;

  // Read encoder states
  uint8_t stateA = digitalRead(ENCODER_A_PIN);
  uint8_t stateB = digitalRead(ENCODER_B_PIN);

  if (stateA != lastStateA || stateB != lastStateB) {
    if (stateA == HIGH && lastStateA == LOW) {
      if (stateB == LOW) {
        // Encoder turned clockwise
        currentColorIndex = (currentColorIndex + 1) % NUM_COLORS;
      } else {
        // Encoder turned counterclockwise
        currentColorIndex = (currentColorIndex - 1 + NUM_COLORS) % NUM_COLORS;
      }
    } else if (stateA == LOW && lastStateA == HIGH) {
      if (stateB == HIGH) {
        // Encoder turned clockwise
        currentColorIndex = (currentColorIndex + 1) % NUM_COLORS;
      } else {
        // Encoder turned counterclockwise
        currentColorIndex = (currentColorIndex - 1 + NUM_COLORS) % NUM_COLORS;
      }
    }
    lastStateA = stateA;
    lastStateB = stateB;

    // Update current LED color
    FastLED.clear();
    gameBoard[currentRow][selectedColorCount] = colorPool[currentColorIndex];
    updateLEDMatrix();
  }

  // Read button state and shift into history
  buttonHistory = (buttonHistory << 1) | digitalRead(BUTTON_PIN);

  // Register a click if button is pressed (history has a 0) and the previous value was not 0
  if (buttonHistory == 0xFE) {
    // Button pressed, lock in the current color
    selectedColors[selectedColorCount] = colorPool[currentColorIndex];
    selectedColorCount++;

    // Move to the next LED or row
    if (selectedColorCount < 3) {
      currentLED = currentRow * 6 + selectedColorCount;
    } else {
      // Provide hints
      provideHints();
      if (isGameWon()) {
        // After winning, publish a message to MQTT
        client.publish("home/puzzle_switch", "on");
        flashWinMessage();
        while (true) {};
      }
      
      currentRow++;
      selectedColorCount = 0;
      currentLED = currentRow * 6;
    }

    // Update current LED color
    FastLED.clear();
    gameBoard[currentRow][selectedColorCount] = colorPool[currentColorIndex];
    updateLEDMatrix();
    
    // Add delay to debounce the button
    delay(750);
  }
}

// Helper function to check if a color is already selected
bool isColorSelected(CRGB colors[], int count, CRGB color) {
  for (int i = 0; i < count; i++) {
    if (colors[i] == color) {
      return true;
    }
  }
  return false;
}

// Function to update the LED matrix
void updateLEDMatrix() {
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 6; j++) {
      leds[i * 6 + j] = gameBoard[i][j];
    }
  }
  FastLED.show();
}

// Function to provide hints after each guess
void provideHints() {
  for (int i = 0; i < 3; i++) {
    if (selectedColors[i] == secretColors[i]) {
      // Correct color and position
      gameBoard[currentRow][3 + i] = CRGB::Green;
    } else if (isColorInSecret(selectedColors[i])) {
      // Correct color, wrong position
      gameBoard[currentRow][3 + i] = CRGB::Yellow;
    } else {
      // Incorrect color
      gameBoard[currentRow][3 + i] = CRGB::Black;
    }
    updateLEDMatrix();
    delay(300);
  }
}

// Helper function to check if a color is in the secret code
bool isColorInSecret(CRGB color) {
  for (int i = 0; i < 3; i++) {
    if (secretColors[i] == color) {
      return true;
    }
  }
  return false;
}

// Function to check if the game is won
bool isGameWon() {
  for (int i = 0; i < 3; i++) {
    if (selectedColors[i] != secretColors[i]) {
      return false;
    }
  }
  return true;
}

// Function to flash a win message
void flashWinMessage() {
  for (int i = 0; i < 3; i++) {
    FastLED.clear();
    FastLED.show();
    delay(250);
    for (int j = 0; j < 36; j++) {
      leds[j] = CRGB::Green;
    }
    FastLED.show();
    delay(250);
  }
}
