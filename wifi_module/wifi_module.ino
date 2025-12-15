String incomingCommand = ""; // Variable to store the incoming command

void setup() {
  Serial.begin(9600);
  Serial.println("\nESP32 is ready to receive commands.");
}

void loop() {
  if (Serial.available() > 0) {
    incomingCommand = Serial.readStringUntil('\n');
    incomingCommand.trim();
    Serial.println("Received: " + incomingCommand);

    // Process the received command
    // if (incomingCommand == "ON") {
    //   Serial.println("Received: ON - Turning LED on.");
    //   // Add code here to turn an LED on
    // } else if (incomingCommand == "OFF") {
    //   Serial.println("Received: OFF - Turning LED off.");
    //   // Add code here to turn an LED off
    // } else {
    //   Serial.println("Received: " + incomingCommand);
    //   Serial.println("Unknown command.");
    // }
  }
}
