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

// Effectively 'registers' on the host's side.
#define CMD_INVALID         0x00
#define CMD_POWER_ENABLE    0x01
#define CMD_POWER_DISABLE   0x02
#define CMD_RESET_DEVICE    0x03
#define CMD_SPI_SELECT      0x04
#define CMD_MBUS_BAUD_RATE  0x05
#define CMD_SERIAL_WRITE    0x06
#define CMD_REGISTER_WRITE  0x07

#define DEVICE_NONE      0x00
#define DEVICE_GPS       0x01
#define DEVICE_MBUS      0x02
#define DEVICE_CAMERA    0x03
#define DEVICE_LORA      0x04
#define DEVICE_LORA_TXRX 0x05
#define DEVICE_SD        0x06
// -- gap -- //
#define DEVICE_VERSION    0xff

// System State (virtual registers)
static volatile uint8_t rIRQ = 0x00;
static volatile uint8_t rButtons = 0x00;

SoftwareSerial gpsSerial( GPS_DATA , -1 ); // RX only
SoftwareSerial mbusSerial( MBUS_RX, MBUS_TX ); // RX and TX

uint8_t mbusRxStorage[256] = {0};
uint8_t gpsStorage[256] = {0};
ByteBuffer mbusRxBuffer;
ByteBuffer gpsBuffer;

static volatile uint8_t TEST_REG_VALUE = 0;
static volatile uint8_t pending_reg = 0;
static volatile bool deferred_reg_read = false;

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

  // Bidirectional TXRX pin for LoRa, default to input (RX mode, high-z)
  pinMode( LORA_TXRX, INPUT );

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
  buffer_init( &gpsBuffer, gpsStorage, sizeof(gpsStorage) );

  // mBus Serial Setup //
  mbusSerial.begin( 9600 );
  buffer_init( &mbusRxBuffer, mbusRxStorage, sizeof(mbusRxStorage) );

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
    if( buffer_full( &gpsBuffer ) ) {
      // Buffer overflow, start dropping data
      buffer_pop( &gpsBuffer, nullptr );
    }

    uint8_t gpsByte = gpsSerial.read();
    buffer_push( &gpsBuffer, gpsByte );
    
    // Only signal IRQ when we have a complete line or significant data
    //if( gpsByte == '\n' || buffer_size( &gpsBuffer ) >= 64 ) {
      //digitalWrite( MBUS_INT, HIGH );
      rIRQ |= IRQ_GPS;
      digitalWrite( HOST_IRQ, LOW ); // Signal interrupt to host
    //}
  }

  if( mbusSerial.available() ) {
    if( buffer_full( &mbusRxBuffer ) ) {
      // Buffer overflow, start dropping data
      buffer_pop( &mbusRxBuffer, nullptr );
    }

    uint8_t mbusByte = mbusSerial.read();
    buffer_push( &mbusRxBuffer, mbusByte );
    
    // Only signal IRQ when we have a complete line or significant data
    //digitalWrite( MBUS_INT, HIGH );
    rIRQ |= IRQ_MBUS;
    digitalWrite( HOST_IRQ, LOW ); // Signal interrupt to host
  }

  delay(2);
}

void i2cRequest() {

  // Handle deferred register read if pending.
  // This allows us to do pin/register reads in the context of the request.
  if( deferred_reg_read ) {
    deferred_reg_read = false;
    uint8_t value = 0x00;
    switch( pending_reg ) {
      case DEVICE_NONE:
        value = TEST_REG_VALUE;
        break;
        
      case DEVICE_LORA_TXRX:
        pinMode( LORA_TXRX, INPUT );
        value = digitalRead( LORA_TXRX );
        break;
      
      case DEVICE_VERSION:
        value = RIBBIT_VERSION;
        break;
    }

    Wire.write( value );
    return;
  }

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
  if( (rIRQ & IRQ_BUTTON) == IRQ_BUTTON ) {
    uint8_t frame[16] = {
      IRQ_BUTTON,
      rButtons,
      0x00
    }; // Zero pad the remaining bytes
    Wire.write( frame, sizeof(frame) );
    rIRQ &= ~IRQ_BUTTON; // Clear button IRQ after read
  }
  else if( (rIRQ & IRQ_GPS) == IRQ_GPS ) {
    uint8_t length = buffer_size( &gpsBuffer );

    if( length > 14 )
      length = 14; // Max data length we can send in one frame

    uint8_t frame[16] = {
      IRQ_GPS,
      length,
    }; // Zero pad the remaining bytes

    for( size_t i = 0; i < length; i++ ) {
      if( buffer_available( &gpsBuffer ) ) {
        uint8_t value = 0;
        buffer_pop( &gpsBuffer, &value );
        frame[i + 2] = value;
      } else {
        frame[i + 2] = 0x00; // No more data
      }
    }

    Wire.write( frame, sizeof(frame) );

    // Keep GPS IRQ asserted if more data remains so the host keeps reading.
    if( buffer_available( &gpsBuffer ) ) {
      rIRQ |= IRQ_GPS;
      digitalWrite( HOST_IRQ, LOW );
      //digitalWrite( MBUS_INT, HIGH ); // Debug
    } else {
      rIRQ &= ~IRQ_GPS; // Clear GPS IRQ after read
    }
  }
  else if( (rIRQ & IRQ_MBUS) == IRQ_MBUS ) {
    uint8_t length = buffer_size( &mbusRxBuffer );

    if( length > 14 )
      length = 14; // Max data length we can send in one frame

    uint8_t frame[16] = {
      IRQ_MBUS,
      length,
    }; // Zero pad the remaining bytes

    for( size_t i = 0; i < length; i++ ) {
      if( buffer_available( &mbusRxBuffer ) ) {
        uint8_t value = 0;
        buffer_pop( &mbusRxBuffer, &value );
        frame[i + 2] = value;
      } else {
        frame[i + 2] = 0x00; // No more data
      }
    }

    Wire.write( frame, sizeof(frame) );

    // Keep mBus IRQ asserted if more data remains so the host keeps reading.
    if( buffer_available( &mbusRxBuffer ) ) {
      rIRQ |= IRQ_MBUS;
      digitalWrite( HOST_IRQ, LOW );
      //digitalWrite( MBUS_INT, HIGH );
    } else {
      rIRQ &= ~IRQ_MBUS; // Clear mBus IRQ after read
    }
  }

  // Only clear the IRQ lines if no other types are pending
  if( rIRQ == IRQ_NONE ) {
    digitalWrite( HOST_IRQ, HIGH ); // Clear interrupt signal
    //digitalWrite( MBUS_INT, LOW ); // Clear mBus interrupt signal
  }
}

void i2cEvent( int length ) {
  uint8_t command = Wire.read();

  // 1-byte commands are just a pin read, and we only currently have 1 pin
  //
  // This is a little bit of a hack to reuse the device IDs as pin selectors for
  // single simple read commands, a-la regular registre reads.
  if( length == 1 ) {
    pending_reg = command;
    deferred_reg_read = true;
    return;
  }

  // All other commands should have at least 2 bytes (command + target)
  uint8_t target = Wire.read();

  // 2-byte commands are configuration commands, so handle those first.
  if( length == 2 ) {
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
      
      case CMD_SPI_SELECT:
        switch( target ) {
          case DEVICE_CAMERA:
            digitalWrite( SD_SELECT, HIGH ); // Deselect SD
            digitalWrite( LORA_SELECT, HIGH ); // Deselect LoRa
            digitalWrite( CAM_SELECT, LOW ); // Enable Camera
            break;
          
          case DEVICE_SD:
            digitalWrite( LORA_SELECT, HIGH ); // Deselect LoRa
            digitalWrite( CAM_SELECT, HIGH ); // Disable Camera
            digitalWrite( SD_SELECT, LOW ); // Select SD
            break;
          
          case DEVICE_LORA:
            digitalWrite( SD_SELECT, HIGH ); // Deselect SD
            digitalWrite( CAM_SELECT, HIGH ); // Disable Camera
            digitalWrite( LORA_SELECT, LOW ); // Select LoRa
            break;

          default:
            // Invalid device
            digitalWrite( SD_SELECT, HIGH ); // Deselect both
            digitalWrite( LORA_SELECT, HIGH );
            digitalWrite( CAM_SELECT, HIGH ); // Disable Camera
            break;
        }
        break;
      
      case CMD_MBUS_BAUD_RATE:
        if( target < sizeof(BAUD_RATES) / sizeof(BAUD_RATES[0]) ) {
          uint32_t baudRate = BAUD_RATES[target];
          mbusSerial.end();
          mbusSerial.begin( baudRate );
        }
        break;

      // Invalid command
      default:
        break;
    }
  }

  // Special cases for 3-byte commands for register writes
  if( command == CMD_REGISTER_WRITE && length == 3 ) {
    uint8_t value = Wire.read();

    switch( target ) {
      case DEVICE_NONE:
        TEST_REG_VALUE = value;
        break;
      
      case DEVICE_LORA_TXRX:
        pinMode( LORA_TXRX, OUTPUT );
        digitalWrite( LORA_TXRX, value == 0 ? LOW : HIGH );
        break;
    }
    return;
  }

  // This must be a serial write of some type, so pull the whole frame and forward it.
  if( command != CMD_SERIAL_WRITE ) {
    // Invalid command for >2 byte frame, drop the data
    for( int i = 0; i < length - 2; i++ )
      Wire.read(); // Discard any remaining bytes
    return;
  }

  for( int i = 0; i < length - 2; i++ ) {
    if( Wire.available() ) {
      uint8_t value = Wire.read();
      switch( target ) {
        case DEVICE_MBUS:
          mbusSerial.write( value );
          mbusSerial.flush();
          break;
        
        default:
          // Invalid device, drop the data
          break;
      }
    }
  }

}