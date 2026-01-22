#include <Arduino.h>
#include <Wire.h>
#include <SoftwareSerial.h>

#include "config.h"
#include "buffer.h"

#define IRQ_NONE   0x00
#define IRQ_BUTTON 0x01
#define IRQ_GPS    0x02
#define IRQ_MBUS   0x04
#define IRQ_CAMERA 0x08
#define IRQ_LORA   0x10

#define CMD_INVALID       0x00
#define CMD_POWER_ENABLE  0x01
#define CMD_POWER_DISABLE 0x02
#define CMD_RESET_DEVICE  0x03

#define DEVICE_INVALID 0x00
#define DEVICE_GPS     0x01
#define DEVICE_MBUS    0x02
#define DEVICE_CAMERA  0x03
#define DEVICE_LORA    0x04
#define DEVICE_SD      0x05

// System State (virtual registers)
uint8_t rIRQ = 0x00;
uint8_t rButtons = 0x00;

SoftwareSerial gpsSerial( GPS_DATA , -1 ); // RX only
SoftwareSerial mbusSerial( MBUS_RX, MBUS_TX ); // RX and TX

uint8_t mbusRxBuffer[256] = {0};
uint8_t gpsBuffer[256] = {0};

// Override any variant-provided initVariant() that might configure pins (e.g., LED_BUILTIN).
extern "C" void initVariant(void)
{
  // Intentionally empty: we want full control of all GPIO configuration.
}

void i2cEvent( int howMany );
void i2cRequest();

void setup() {
  UCSR0B = 0; // Disable UART0

  // Power Control Pins //
  pinMode( MBUS_EN, OUTPUT );
  digitalWrite( MBUS_EN, LOW );

  pinMode( HOST_IRQ, OUTPUT );
  digitalWrite( HOST_IRQ, HIGH );

  pinMode( GPS_EN, OUTPUT );
  digitalWrite( GPS_EN, LOW );

  pinMode( CAM_EN, OUTPUT );
  digitalWrite( CAM_EN, LOW );

  // SPI Select Pins //
  pinMode( SD_SELECT, OUTPUT );
  digitalWrite( SD_SELECT, HIGH );

  pinMode( LORA_SELECT, OUTPUT );
  digitalWrite( LORA_SELECT, HIGH );

  // Digital IO //
  pinMode( SW_0, INPUT );
  pinMode( SW_1, INPUT );
  pinMode( SW_2, INPUT );
  pinMode( SW_3, INPUT );
  pinMode( SW_4, INPUT );
  pinMode( SW_5, INPUT );

  // mBus Port Setup //
  pinMode( MBUS_INT, OUTPUT );
  digitalWrite( MBUS_INT, LOW );
  pinMode( MBUS_RESET, OUTPUT );
  digitalWrite( MBUS_RESET, LOW ); // ToDo: change to HIGH for normal boards

  // GPS Serial Setup //
  gpsSerial.begin( 9600 );
  buffer_init( gpsBuffer, sizeof(gpsBuffer) );

  // mBus Serial Setup //
  //mbusSerial.begin( 9600 );
  //buffer_init( mbusRxBuffer, sizeof(mbusRxBuffer) );

  // I2C Setup //
  Wire.begin( RIBBIT_ADDRESS );
  Wire.onReceive( i2cEvent );
  Wire.onRequest( i2cRequest );
}

uint8_t oldState = 0;
void loop() {
  rButtons = (digitalRead(SW_0) == LOW ? 1 : 0) << 0;
  rButtons |= (digitalRead(SW_1) == LOW ? 1 : 0) << 1;
  rButtons |= (digitalRead(SW_2) == LOW ? 1 : 0) << 2;
  rButtons |= (digitalRead(SW_3) == LOW ? 1 : 0) << 3;
  rButtons |= (digitalRead(SW_4) == LOW ? 1 : 0) << 4;
  rButtons |= (digitalRead(SW_5) == LOW ? 1 : 0) << 5;
  
  // Button state changed, trigger IRQ
  if( rButtons != oldState ) {
    oldState = rButtons;
    rIRQ |= IRQ_BUTTON;
    digitalWrite( HOST_IRQ, LOW ); // Signal interrupt to host
  }

  if( gpsSerial.available() ) {
    uint8_t gpsByte = gpsSerial.read();
    buffer_push( gpsBuffer, sizeof(gpsBuffer), gpsByte );
    
    // Always flag IRQ on new data
    digitalWrite( MBUS_INT, HIGH );

    rIRQ |= IRQ_GPS;
    digitalWrite( HOST_IRQ, LOW ); // Signal interrupt to host
  }
}

void i2cRequest() {

  if( rIRQ == IRQ_NONE ) {
    uint8_t frame[16] = {0};
    Wire.write( frame, sizeof(frame) );
    return;
  }

  // The following sections should be mutually exclusive
  // and in descending priority order.
  //
  // Returned frames should have their header IRQ set to a single
  // bit option, not a composite!
  if( (rIRQ & IRQ_GPS) == IRQ_GPS ) {
    uint8_t length = buffer_size( gpsBuffer, sizeof(gpsBuffer ) );

    if( length > 14 )
      length = 14; // Max data length we can send in one frame

    uint8_t frame[16] = {
      IRQ_GPS,
      length,
    }; // Zero pad the remaining bytes

    for( size_t i = 0; i < length; i++ ) {
      if( buffer_available( gpsBuffer, sizeof(gpsBuffer) ) ) {
        frame[i + 2] = buffer_pop( gpsBuffer, sizeof(gpsBuffer) );
      } else {
        frame[i + 2] = 0x00; // No more data
      }
    }

    Wire.write( frame, sizeof(frame) );
    rIRQ &= ~IRQ_GPS; // Clear GPS IRQ after read
  }
  else if( (rIRQ & IRQ_BUTTON) == IRQ_BUTTON ) {
    uint8_t frame[16] = {
      IRQ_BUTTON,
      rButtons,
      0x00
    }; // Zero pad the remaining bytes
    Wire.write( frame, sizeof(frame) );
    rIRQ &= ~IRQ_BUTTON; // Clear button IRQ after read
  }

  // Only clear the IRQ lines if no other types are pending
  if( rIRQ == IRQ_NONE ) {
    digitalWrite( HOST_IRQ, HIGH ); // Clear interrupt signal
    digitalWrite( MBUS_INT, LOW ); // Clear mBus interrupt signal
  }
}

void i2cEvent( int length ) {
  /*if( length < 2 ) {
    // Not enough data to process command
    return;
  }*/

  digitalWrite( MBUS_INT, HIGH );
  digitalWrite( MBUS_RESET, LOW );

  uint8_t command = Wire.read();
  uint8_t target = Wire.read();

  switch( command ) {

    // Enable power to peripheral
    case CMD_POWER_ENABLE:
      switch( target ) {
        case DEVICE_GPS:
          digitalWrite( GPS_EN, HIGH );
          break;
        case DEVICE_MBUS:
          digitalWrite( MBUS_EN, HIGH );
          break;
        case DEVICE_CAMERA:
          digitalWrite( CAM_EN, HIGH );
          break;
        default:
          // Invalid device
          break;
      }
      break;

    // Disable power to peripheral
    case CMD_POWER_DISABLE:
      switch( target ) {
        case DEVICE_GPS:
          digitalWrite( GPS_EN, LOW );
          break;
        case DEVICE_MBUS:
          digitalWrite( MBUS_EN, LOW );
          break;
        case DEVICE_CAMERA:
          digitalWrite( CAM_EN, LOW );
          break;
        default:
          // Invalid device
          break;
      }
      break;

    // Reset peripheral device
    case CMD_RESET_DEVICE:
      switch( target ) {
        case DEVICE_GPS:
          digitalWrite( GPS_EN, LOW );
          delay( 50 );
          digitalWrite( GPS_EN, HIGH );
          break;
        case DEVICE_MBUS:
          digitalWrite( MBUS_EN, LOW );
          delay( 50 );
          digitalWrite( MBUS_EN, HIGH );
          break;
        case DEVICE_CAMERA:
          digitalWrite( CAM_EN, LOW );
          delay( 50 );
          digitalWrite( CAM_EN, HIGH );
          break;
        default:
          // Invalid device
          break;
      }
      break;

    // Invalid command
    default:
      digitalWrite( MBUS_RESET, HIGH );
      break;
  }

  delay( 10 );
  digitalWrite( MBUS_INT, LOW );

  /*while ( Wire.available() ) {
    uint8_t receivedByte = Wire.read();
    if ( receivedByte == 0x01 ) {
      digitalWrite( MBUS_EN, HIGH );
    } else if ( receivedByte == 0x00 ) {
      digitalWrite( MBUS_EN, LOW );
    }
  }*/
}