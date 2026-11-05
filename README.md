# Agri-Rover

A mobile IoT-controlled agricultural rover designed to assist with basic farming operations such as ploughing, seed sowing, and water spraying.

The rover is built around an ESP32 microcontroller and can be controlled wirelessly through an Android application using Bluetooth.

## Features

- Bluetooth-based manual control
- Plough mechanism controlled by a servo motor
- Automatic seed dispensing mechanism
- LDR-based seed availability detection
- Water spraying system using a mini DC pump
- Water-level monitoring
- Buzzer and LED indicators for system alerts
- Expandable design for future camera-based monitoring

## Hardware Components

- ESP32 development board
- ESP32-CAM
- TB6612FNG motor driver
- Four DC gear motors and wheels
- MG90S servo motors
- LDR sensor
- Water-level sensor
- Mini DC water pump
- Buzzer and LEDs
- Battery pack and voltage regulators
- Chassis, plough, seed container, and other supporting components

## Software

- Arduino IDE
- C++ (Arduino framework)
- ESP32 Arduino Core
- BluetoothSerial library
- ESP32Servo library
- Android application for Bluetooth control

## Project Structure

```text
Agri-Rover/
├── Application/
│   ├── Screenshots/
│   └── Source_Code/
├── Documentation/
│   ├── Presentation/
│   └── Reports/
├── Media/
│   ├── Photos/
│   └── Videos/
├── Source_Code/
│   ├── Embedded_System/
│   └── Other_Modules/
├── LICENSE
└── README.md
```

## Future Improvements

- ESP32-CAM integration for live video monitoring
- Remote monitoring through a camera interface
- Automated pesticide spraying
- Improved navigation and autonomous operation

