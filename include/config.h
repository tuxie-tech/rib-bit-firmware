#pragma once

/**
 * NOTE! These must always be Arduino-style pin numbers, not port/pin definitions.
 * Use the PIN_Pxy definitions from pins_arduino.h to define them correctly.
 * 
 * It'll build just fine, but the pin mappings will be wrong if you use
 * port/pin definitions directly here.
 */

// Pin Definitions // Citizen Bit Hardware Version 1.0
#define HOST_IRQ    PIN_PE1
#define MBUS_RESET  PIN_PE0
#define GPS_RESET   PIN_PE2
#define CAM_SELECT  PIN_PE3

// SPI0 (PB3..PB5, PB4=MISO) on classic mapping: D11..D13, D12
#define MISO_0  PIN_PB4  // PB4
#define MOSI_0  PIN_PB3  // PB3
#define SCK_0   PIN_PB5  // PB5

// Switches
#define SW_0    PIN_PB2  // PB2
#define SW_1    PIN_PB1  // PB1
#define SW_2    PIN_PB0  // PB0
#define SW_3    PIN_PD3  // PD3
#define SW_4    PIN_PD2  // PD2
#define SW_5    PIN_PD1  // PD1

// GPS
#define GPS_DATA  PIN_PD0  // PD0
#define GPS_EN    PIN_PD5  // PD5

// MBUS
#define MBUS_EN   PIN_PD4   // PD4
#define MBUS_TX   PIN_PB3   // PB3
#define MBUS_RX   PIN_PB4   // PB4
#define MBUS_INT  PIN_PB5   // PB5

// Camera / SD
#define CAM_EN     PIN_PD7 // PD7
#define SD_SELECT  PIN_PD6 // PD6

// LoRa (PC0..PC2 => A0..A2)
#define LORA_SELECT PIN_PC0 // PC0
#define LORA_RESET  PIN_PC1 // PC1
#define LORA_TXRX   PIN_PC2 // PC2

// I2C (PC4/PC5 => A4/A5)
#define SYS_SDA PIN_PC4 // PC4
#define SYS_SCL PIN_PC5 // PC5


// Configuration //
#define RIBBIT_ADDRESS 0x10
#define RIBBIT_VERSION 4