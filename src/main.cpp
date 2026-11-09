#include <Arduino.h>
#include <Wire.h>
#include <avr/wdt.h>

#include "config.h"
#include "buffer.h"

// Debug Flags
#define DEBUG_FLAG_NONE 0x00
#define DEBUG_FLAG_I2C_TRAFFIC 0x01

// IRQ masks
#define IRQ_NONE        0x00
#define IRQ_BUTTON      0x01
#define IRQ_GPS         0x02
#define IRQ_MBUS        0x04
#define IRQ_MBUS_INT    0x08
#define IRQ_MBUS_RESET  0x10
#define IRQ_MBUS_SERIAL 0x20
#define IRQ_CAMERA      0x40
#define IRQ_LORA        0x80

// Effectively 'registers' on the host's side.
#define REG_INVALID             0x00
#define REG_DEBUG               0x01 // write-only
#define REG_VERSION             0x01 // read-only

#define REG_GPS_CONTROL         0x02 // write-only
#define REG_GPS_STATUS          0x02 // read-only

#define REG_MBUS_CONTROL        0x03 // write-only
#define REG_MBUS_STATUS         0x03 // read-only

#define REG_CAMERA_CONTROL      0x04 // write-only
#define REG_CAMERA_STATUS       0x04 // read-only

#define REG_LORA_CONTROL        0x05 // write-only
#define REG_LORA_STATUS         0x05 // read-only

#define REG_SD_CONTROL          0x06 // write-only
#define REG_SD_STATUS           0x06 // read-only

#define REG_BUTTONS_STATE       0x07 // read-only, bitmask of button states (1=pressed, 0=released)
#define REG_BUTTONS_IRQ         0x07 // write-only, bitmask of buttons that should trigger an IRQ when their state changes

#define REG_MBUS_INT_DIGITAL    0x08 // read/write - places the pin into digital IO mode if accessed
#define REG_MBUS_INT_ANALOG     0x09 // read/write - places the pin into analog mode if accessed, and returns the analog value on read, or sets a PWM value on write
                                     // Inoperable (invalid state) if interrupts are enabled on this pin.

#define REG_MBUS_RESET_DIGITAL  0x0A // read/write - places the pin into digital IO mode if accessed
#define REG_MBUS_RESET_ANALOG   0x0B // read/write - places the pin into analog mode if accessed, and returns the analog value on read, or sets a PWM value on write
                                     // Inoperable (invalid state) if interrupts are enabled on this pin.

#define REG_MBUS_SERIAL_CONFIG       0x0C // Multibyte register: [baud, bits, stop, parity]
#define REG_MBUS_SERIAL_DATA         0x0D // FIFO buffer for serial data to be sent out, or data received from the host
#define REG_MBUS_SERIAL_LENGTH       0x0E // Number of bytes in the serial data buffer

#define REG_GPS_DATA            0x0F // FIFO buffer for GPS data
#define REG_GPS_LENGTH          0x10 // Number of bytes in the GPS data buffer

// ... gap! ...

#define REG_SEQUENCE            0xFE // Incrementing value that the host can use to detect missed updates and reboots
#define REG_IRQ                 0xFF // read/write - reads return the current IRQ status without clearing it, writes will override the current IRQ status.

// Used to ensure a stable interface to potentially differing Arduino constants across versions and platforms.
// Note that this must match the lookup table in the firmware
typedef enum {
  CONFIG_5N1 = 0x00, // SERIAL_5N1,
  CONFIG_6N1 = 0x01, // SERIAL_6N1,
  CONFIG_7N1 = 0x02, // SERIAL_7N1,
  CONFIG_8N1 = 0x03, // SERIAL_8N1,
  CONFIG_5N2 = 0x04, // SERIAL_5N2,
  CONFIG_6N2 = 0x05, // SERIAL_6N2,
  CONFIG_7N2 = 0x06, // SERIAL_7N2,
  CONFIG_8N2 = 0x07, // SERIAL_8N2,
  CONFIG_5E1 = 0x08, // SERIAL_5E1,
  // even parity
  CONFIG_6E1 = 0x09, // SERIAL_6E1,
  CONFIG_7E1 = 0x0A, // SERIAL_7E1,
  CONFIG_8E1 = 0x0B, // SERIAL_8E1,
  CONFIG_5E2 = 0x0C, // SERIAL_5E2,
  CONFIG_6E2 = 0x0D, // SERIAL_6E2,
  CONFIG_7E2 = 0x0E, // SERIAL_7E2,
  CONFIG_8E2 = 0x0F, // SERIAL_8E2,
  CONFIG_5O1 = 0x10, // SERIAL_5O1,
  // odd parity
  CONFIG_6O1 = 0x11, // SERIAL_6O1,
  CONFIG_7O1 = 0x12, // SERIAL_7O1,
  CONFIG_8O1 = 0x13, // SERIAL_8O1,
  CONFIG_5O2 = 0x14, // SERIAL_5O2,
  CONFIG_6O2 = 0x15, // SERIAL_6O2,
  CONFIG_7O2 = 0x16, // SERIAL_7O2,
  CONFIG_8O2 = 0x17  // SERIAL_8O2
} mbus_serial_config_t;

// Used for select mutual exclusion
#define DEV_NONE    0x00
#define DEV_MBUS    0x01
#define DEV_CAMERA  0x02
#define DEV_LORA    0x03
#define DEV_SD      0x04


// System State (virtual registers)
static volatile uint8_t rSequence = 0x00;
static volatile uint8_t rSystemFlags = DEBUG_FLAG_NONE;
static volatile uint8_t rIRQ = 0x00;
static volatile uint8_t rButtons = 0x00;
static volatile uint8_t rButtonsIRQMask = 0x00;

// Power control state
static volatile bool fGpsPower = false;
static volatile bool fMbusPower = false;
static volatile bool fCameraPower = false;

// Select state for SPI devices (camera, SD, LoRa)
static volatile uint8_t spiSelectState = DEV_NONE;

static HardwareSerial &gpsSerial = Serial;   // USART0 (PD0/PD1)
static HardwareSerial &mbusSerial = Serial1; // USART1 (PB4/PB3)

static uint8_t mbusRxStorage[256] = {0};
static uint8_t gpsStorage[256] = {0};
static ByteBuffer mbusRxBuffer;
static ByteBuffer gpsBuffer;

static volatile uint8_t register_ptr = 0x00;

// Override any variant-provided initVariant() that might configure pins (e.g., LED_BUILTIN).
extern "C" void initVariant(void)
{
  // Intentionally empty: we want full control of all GPIO configuration.
}

uint16_t wireReadU16() {
  if( Wire.available() < 2 )
    return 0;
  
  uint16_t value = Wire.read() << 8; // Read high byte
  value |= Wire.read(); // Read low byte
  return value;
}

uint32_t wireReadU32() {
  if( Wire.available() < 4 )
    return 0;

  uint32_t value = (uint32_t)Wire.read() << 24;
  value |= (uint32_t)Wire.read() << 16;
  value |= (uint32_t)Wire.read() << 8;
  value |= (uint32_t)Wire.read();
  return value;
}

void i2cRemoteWrite( int howMany );
void i2cRemoteRead();

typedef void (*isr_callback_t)(void);

struct PinInterruptHandler {
  isr_callback_t callback;
  uint8_t mode;
  uint8_t lastState;
  bool enabled;
};

static PinInterruptHandler gPinInterruptHandlers[NUM_DIGITAL_PINS] = {};

static bool pinInterruptShouldFire(uint8_t mode, uint8_t lastState, uint8_t currentState)
{
  if( lastState == currentState )
    return false;

  switch( mode ) {
    case CHANGE:
      return true;
    case RISING:
      return (lastState == LOW) && (currentState == HIGH);
    case FALLING:
      return (lastState == HIGH) && (currentState == LOW);
    default:
      return true;
  }
}

static void handlePinChangeGroup(uint8_t group)
{
  for( uint8_t pin = 0; pin < NUM_DIGITAL_PINS; pin++ ) {
    PinInterruptHandler &handler = gPinInterruptHandlers[pin];
    if( !handler.enabled || handler.callback == nullptr )
      continue;

    volatile uint8_t *pcicr = digitalPinToPCICR(pin);
    if( pcicr == nullptr || digitalPinToPCICRbit(pin) != group )
      continue;

    uint8_t bitMask = digitalPinToBitMask(pin);
    volatile uint8_t *input = portInputRegister(digitalPinToPort(pin));
    uint8_t currentState = ((*input) & bitMask) ? HIGH : LOW;

    if( pinInterruptShouldFire(handler.mode, handler.lastState, currentState) )
      handler.callback();

    handler.lastState = currentState;
  }
}

// attachInterrupt-like API with fallback to pin change interrupts when needed.
static bool attachInterruptCompat(uint8_t pin, isr_callback_t callback, int mode)
{
  int interruptNumber = digitalPinToInterrupt(pin);
  if( interruptNumber != NOT_AN_INTERRUPT ) {
    attachInterrupt(interruptNumber, callback, mode);
    return true;
  }

  volatile uint8_t *pcicr = digitalPinToPCICR(pin);
  volatile uint8_t *pcmsk = digitalPinToPCMSK(pin);
  if( pcicr == nullptr || pcmsk == nullptr )
    return false;

  uint8_t bitMask = digitalPinToBitMask(pin);
  volatile uint8_t *input = portInputRegister(digitalPinToPort(pin));

  gPinInterruptHandlers[pin].callback = callback;
  gPinInterruptHandlers[pin].mode = (uint8_t)mode;
  gPinInterruptHandlers[pin].lastState = ((*input) & bitMask) ? HIGH : LOW;
  gPinInterruptHandlers[pin].enabled = true;

  *pcmsk |= _BV(digitalPinToPCMSKbit(pin));
  *pcicr |= _BV(digitalPinToPCICRbit(pin));

  return true;
}

#if defined(PCINT0_vect)
ISR(PCINT0_vect)
{
  handlePinChangeGroup(0);
}
#endif

#if defined(PCINT1_vect)
ISR(PCINT1_vect)
{
  handlePinChangeGroup(1);
}
#endif

#if defined(PCINT2_vect)
ISR(PCINT2_vect)
{
  handlePinChangeGroup(2);
}
#endif

#if defined(PCINT3_vect)
ISR(PCINT3_vect)
{
  handlePinChangeGroup(3);
}
#endif

static void setupWatchdog()
{
  MCUSR &= (uint8_t)~_BV(WDRF);
  wdt_disable();
  wdt_enable( WDTO_8S );
  wdt_reset();
}

static void beginGpsRxOnly(uint32_t baud)
{
  gpsSerial.begin( baud );

  // Keep USART0 RX enabled on PD0 but release PD1 for SW_5 by disabling TX.
  UCSR0B &= (uint8_t)~_BV(TXEN0);
  pinMode( SW_5, INPUT );
}

void buttonISR() {
  rIRQ |= IRQ_BUTTON;
}

static void setDebug( uint8_t led, bool state ) {
  if( led == 0 ) {
    #if defined(DEBUG_LED_A)
      digitalWrite( DEBUG_LED_A, !state );
    #endif
  } else if( led == 1 ) {
    #if defined(DEBUG_LED_B)
      digitalWrite( DEBUG_LED_B, !state );
    #endif
  }
}

void setup() {
  setupWatchdog();

  rSequence = 0x00;
  rSystemFlags = 0x00;
  rIRQ = 0x00;
  rButtons = 0x00;
  rButtonsIRQMask = 0x00;
  fGpsPower = false;
  fMbusPower = false;
  fCameraPower = false;
  spiSelectState = DEV_NONE;

  #if defined(DEBUG_LED_A) && defined(DEBUG_LED_B)
    pinMode( DEBUG_LED_A, OUTPUT );
    pinMode( DEBUG_LED_B, OUTPUT );
  #endif
  setDebug( 0, HIGH );
  setDebug( 1, HIGH );

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

  pinMode( CAM_SELECT, OUTPUT );
  digitalWrite( CAM_SELECT, HIGH );

  // Bidirectional TXRX pin for LoRa, default to input (RX mode, high-z)
  pinMode( LORA_TXRX, INPUT );

  // Digital IO //
  pinMode( SW_0, INPUT_PULLUP );
  pinMode( SW_1, INPUT_PULLUP );
  pinMode( SW_2, INPUT_PULLUP );
  pinMode( SW_3, INPUT_PULLUP );
  pinMode( SW_4, INPUT_PULLUP );
  pinMode( SW_5, INPUT_PULLUP );

  attachInterruptCompat(SW_0, buttonISR, CHANGE);
  attachInterruptCompat(SW_1, buttonISR, CHANGE);
  attachInterruptCompat(SW_2, buttonISR, CHANGE);
  attachInterruptCompat(SW_3, buttonISR, CHANGE);
  attachInterruptCompat(SW_4, buttonISR, CHANGE);
  attachInterruptCompat(SW_5, buttonISR, CHANGE);

  // mBus Port Setup //
  pinMode( MBUS_INT, OUTPUT );
  digitalWrite( MBUS_INT, LOW );
  pinMode( MBUS_RESET, OUTPUT );
  digitalWrite( MBUS_RESET, LOW ); // ToDo: change to HIGH for normal boards

  // GPS Serial Setup //
  beginGpsRxOnly( 9600 );
  buffer_init( &gpsBuffer, gpsStorage, sizeof(gpsStorage) );

  // mBus Serial Setup //
  mbusSerial.begin( 115200 );
  buffer_init( &mbusRxBuffer, mbusRxStorage, sizeof(mbusRxStorage) );

  // I2C Setup //
  Wire.begin( RIBBIT_ADDRESS );
  Wire.onReceive( i2cRemoteWrite );
  Wire.onRequest( i2cRemoteRead );

  delay(100);
  #if defined(DEBUG_LED_B)
    setDebug( 0, LOW );
    setDebug( 1, LOW );
  #endif
}

uint8_t oldState = 0;
void loop() {
  
  // Button state changed, trigger IRQ if we're not IRQ masked
  /*if( rButtons != oldState ) {
    bool triggerIRQ = (rButtons & rButtonsIRQMask) != (oldState & rButtonsIRQMask);
    oldState = rButtons;

    if( triggerIRQ )
      rIRQ |= IRQ_BUTTON;
  }*/

  if( rIRQ != 0 )
    digitalWrite( HOST_IRQ, LOW ); // Signal interrupt to host
  else
    digitalWrite( HOST_IRQ, HIGH ); // Clear interrupt signal if no IRQs pending
}

void serialEvent() {
  while( gpsSerial.available() ) {
    uint8_t byte = gpsSerial.read();
    buffer_push( &gpsBuffer, byte );

    // Trigger an IRQ for the host to read this data
    rIRQ |= IRQ_GPS;
  }
}

void serialEvent1() {
  while( mbusSerial.available() ) {
    uint8_t byte = mbusSerial.read();
    buffer_push( &mbusRxBuffer, byte );

    rIRQ |= IRQ_MBUS_SERIAL;
  }
}

void spi_select( uint8_t device ) {
  if( spiSelectState == device )
    return; // No change
  
  switch( device ) {

    // Note: SPI select for MBUS is handled by the micro:bit, so we just deassert all other devices
    case DEV_MBUS:
      digitalWrite( SD_SELECT, HIGH );
      digitalWrite( LORA_SELECT, HIGH );
      digitalWrite( CAM_SELECT, HIGH );
      spiSelectState = DEV_MBUS;
      break;
    
    case DEV_CAMERA:
      digitalWrite( SD_SELECT, HIGH );
      digitalWrite( LORA_SELECT, HIGH );
      digitalWrite( CAM_SELECT, LOW );
      spiSelectState = DEV_CAMERA;
      break;
    
    case DEV_LORA:
      digitalWrite( SD_SELECT, HIGH );
      digitalWrite( CAM_SELECT, HIGH );
      digitalWrite( LORA_SELECT, LOW );
      spiSelectState = DEV_LORA;

      break;
    
    case DEV_SD:
      digitalWrite( CAM_SELECT, HIGH );
      digitalWrite( LORA_SELECT, HIGH );
      digitalWrite( SD_SELECT, LOW );
      spiSelectState = DEV_SD;
      break;
    
    // Also handles DEV_NONE along with any invalid device selections
    default:
      digitalWrite( SD_SELECT, HIGH );
      digitalWrite( LORA_SELECT, HIGH );
      digitalWrite( CAM_SELECT, HIGH );
      spiSelectState = DEV_NONE;
      device = DEV_NONE; // Ensure invalid device selections are treated as no device selected
      break;
  }

  spiSelectState = device;
}

void i2cRemoteRead() {
  uint8_t buffer = 0x00;

  if( (rSystemFlags & DEBUG_FLAG_I2C_TRAFFIC) == DEBUG_FLAG_I2C_TRAFFIC )
    setDebug( 1, HIGH );

  switch( register_ptr ) {
    case REG_VERSION:
      Wire.write( RIBBIT_VERSION );
      wdt_reset();
      break;
    
    case REG_GPS_STATUS:
      buffer = fGpsPower ? 0x01 : 0x00;
      Wire.write( buffer );
      wdt_reset();
      break;

    case REG_MBUS_STATUS:
      buffer = fMbusPower ? 0x01 : 0x00;
      buffer |= (spiSelectState == DEV_MBUS) ? 0x02 : 0x00;
      Wire.write( buffer );
      wdt_reset();
      break;
    
    case REG_CAMERA_STATUS:
      buffer = fCameraPower ? 0x01 : 0x00;
      buffer |= (spiSelectState == DEV_CAMERA) ? 0x02 : 0x00;
      Wire.write( buffer );
      wdt_reset();
      break;
    
    case REG_LORA_STATUS:
      buffer = 0x01; // No power control
      buffer |= (spiSelectState == DEV_LORA) ? 0x02 : 0x00;
      Wire.write( buffer );
      wdt_reset();
      break;
    
    case REG_SD_STATUS:
      buffer = 0x01; // No power control
      buffer |= (spiSelectState == DEV_SD) ? 0x02 : 0x00;
      Wire.write( buffer );
      wdt_reset();
      break;
    
    case REG_BUTTONS_STATE:
      rButtons = (digitalRead(SW_0) == LOW ? 1 : 0) << 0;
      rButtons |= (digitalRead(SW_1) == LOW ? 1 : 0) << 1;
      rButtons |= (digitalRead(SW_2) == LOW ? 1 : 0) << 2;
      rButtons |= (digitalRead(SW_3) == LOW ? 1 : 0) << 3;
      rButtons |= (digitalRead(SW_4) == LOW ? 1 : 0) << 4;
      rButtons |= (digitalRead(SW_5) == LOW ? 1 : 0) << 5;
      Wire.write( rButtons );
      rIRQ &= ~IRQ_BUTTON; // Clear button IRQ on read
      wdt_reset();
      break;
    
    case REG_MBUS_INT_DIGITAL:
      pinMode( MBUS_INT, INPUT );
      Wire.write( digitalRead(MBUS_INT) );
      wdt_reset();
      break;
    
    case REG_MBUS_INT_ANALOG:
      pinMode( MBUS_INT, INPUT );
      {
        uint16_t value = analogRead( MBUS_INT );
        uint8_t bytes[2] = {
          (uint8_t)(value >> 8),
          (uint8_t)(value & 0xFF)
        };
        Wire.write( bytes, sizeof(bytes) );
      }
      wdt_reset();
      break;
    
    case REG_MBUS_RESET_DIGITAL:
      pinMode( MBUS_RESET, INPUT );
      Wire.write( digitalRead(MBUS_RESET) );
      wdt_reset();
      break;
    
    case REG_MBUS_RESET_ANALOG:
      pinMode( MBUS_RESET, INPUT );
      {
        uint16_t value = analogRead( MBUS_RESET );
        uint8_t bytes[2] = {
          (uint8_t)(value >> 8),
          (uint8_t)(value & 0xFF)
        };
        Wire.write( bytes, sizeof(bytes) );
      }
      wdt_reset();
      break;

    case REG_MBUS_SERIAL_CONFIG:
      Wire.write( 0x00 ); // Todo
      wdt_reset();
      break;
    
    case REG_MBUS_SERIAL_DATA:
      if( buffer_available( &mbusRxBuffer ) ) {
        uint8_t data;
        buffer_pop( &mbusRxBuffer, &data );
        Wire.write( data );

        if( !buffer_available( &mbusRxBuffer ) )
          rIRQ &= ~IRQ_MBUS_SERIAL; // Clear MBUS Serial IRQ on read
      } else {
        Wire.write( 0x00 ); // No data available
      }
      wdt_reset();
      break;
    
    case REG_MBUS_SERIAL_LENGTH:
      Wire.write( buffer_size( &mbusRxBuffer ) );
      wdt_reset();
      break;
    
    case REG_GPS_DATA:
      if( buffer_available( &gpsBuffer ) ) {
        uint8_t data;
        buffer_pop( &gpsBuffer, &data );
        Wire.write( data );

        if( !buffer_available( &gpsBuffer ) )
          rIRQ &= ~IRQ_GPS; // Clear GPS IRQ on read
      } else {
        Wire.write( 0x00 ); // No data available
      }
      wdt_reset();
      break;
    
    case REG_GPS_LENGTH:
      Wire.write( buffer_size( &gpsBuffer ) );
      wdt_reset();
      break;
    
    case REG_SEQUENCE:
      Wire.write( rSequence );
      wdt_reset();
      break;
    
    case REG_IRQ:
      Wire.write( rIRQ );
      wdt_reset();
      break;
    
    default:
      Wire.write( 0x00 ); // Default read value for unimplemented registers
      wdt_reset();
      break;
  }

  if( (rSystemFlags & DEBUG_FLAG_I2C_TRAFFIC) == DEBUG_FLAG_I2C_TRAFFIC )
    setDebug( 1, LOW );

  rSequence++;
  if( rSequence == 0x00 )
    rSequence = 0x01; // Ensure sequence number never returns to 0 to avoid confusion with power-on state
}

void i2cRemoteWrite( int length ) {
  if( (rSystemFlags & DEBUG_FLAG_I2C_TRAFFIC) == DEBUG_FLAG_I2C_TRAFFIC )
    setDebug( 0, HIGH );
  
  register_ptr = Wire.read(); // First byte is always the register address
  uint8_t buffer = 0x00;

  if( length == 1 )
    return; // No data bytes, just a register pointer update

  if( Wire.available() < 1 )
    return; // No data byte to read, invalid write

  switch( register_ptr ) {

    // Set any system flags
    case REG_DEBUG:
      rSystemFlags = Wire.read();
      wdt_reset();
      break;
    
    case REG_GPS_CONTROL:
      buffer = Wire.read();
      fGpsPower = (buffer & 0x01) != 0;
      digitalWrite( GPS_EN, fGpsPower ? HIGH : LOW );
      wdt_reset();
      break;
    
    case REG_MBUS_CONTROL:
      buffer = Wire.read();
      
      // Power state
      fMbusPower = (buffer & 0x01) != 0;
      digitalWrite( MBUS_EN, fMbusPower ? HIGH : LOW );

      // SPI select state
      spi_select( (buffer & 0x02) != 0 ? DEV_MBUS : DEV_NONE );

      wdt_reset();
      break;
    
    case REG_CAMERA_CONTROL:
      buffer = Wire.read();

      // Power state
      fCameraPower = (buffer & 0x01) != 0;
      digitalWrite( CAM_EN, fCameraPower ? HIGH : LOW );

      // SPI select state
      spi_select( (buffer & 0x02) != 0 ? DEV_CAMERA : DEV_NONE );

      wdt_reset();
      break;
    
    case REG_LORA_CONTROL:
      buffer = Wire.read();
      
      // SPI select state
      spi_select( (buffer & 0x02) != 0 ? DEV_LORA : DEV_NONE );

      wdt_reset();
      break;
    
    case REG_SD_CONTROL:
      buffer = Wire.read();

      // SPI select state
      spi_select( (buffer & 0x02) != 0 ? DEV_SD : DEV_NONE );

      wdt_reset();
      break;
    
    case REG_BUTTONS_IRQ:
      rButtonsIRQMask = Wire.read();
      wdt_reset();
      break;
    
    case REG_MBUS_INT_DIGITAL:
      pinMode( MBUS_INT, OUTPUT );
      digitalWrite( MBUS_INT, (Wire.read() & 0x01) != 0 ? HIGH : LOW );
      wdt_reset();
      break;
    
    case REG_MBUS_INT_ANALOG:
      {
        uint16_t value = wireReadU16();
        pinMode( MBUS_INT, OUTPUT );
        analogWrite( MBUS_INT, value );
        wdt_reset();
      }
      break;
    
    case REG_MBUS_RESET_DIGITAL:
      pinMode( MBUS_RESET, OUTPUT );
      digitalWrite( MBUS_RESET, (Wire.read() & 0x01) != 0 ? HIGH : LOW );
      wdt_reset();
      break;
    
    case REG_MBUS_RESET_ANALOG:
      {
        uint16_t value = wireReadU16();
        pinMode( MBUS_RESET, OUTPUT );
        analogWrite( MBUS_RESET, value );
        wdt_reset();
      }
      break;
    
    case REG_MBUS_SERIAL_CONFIG:
      {
        if( Wire.available() < 5 )
          break;

        // Baud. bits, stop, parity
        uint32_t baud = wireReadU32();

        if( baud > 2000000 )
          baud = 2000000; // Cap baud rate to a reasonable maximum to prevent overflow issues

        uint8_t config = Wire.read();
        switch( config ) {
          case CONFIG_5N1: config = SERIAL_5N1; break;
          case CONFIG_6N1: config = SERIAL_6N1; break;
          case CONFIG_7N1: config = SERIAL_7N1; break;
          case CONFIG_8N1: config = SERIAL_8N1; break;
          case CONFIG_5N2: config = SERIAL_5N2; break;
          case CONFIG_6N2: config = SERIAL_6N2; break;
          case CONFIG_7N2: config = SERIAL_7N2; break;
          case CONFIG_8N2: config = SERIAL_8N2; break;
          case CONFIG_5E1: config = SERIAL_5E1; break;
          case CONFIG_6E1: config = SERIAL_6E1; break;
          case CONFIG_7E1: config = SERIAL_7E1; break;
          case CONFIG_8E1: config = SERIAL_8E1; break;
          case CONFIG_5E2: config = SERIAL_5E2; break;
          case CONFIG_6E2: config = SERIAL_6E2; break;
          case CONFIG_7E2: config = SERIAL_7E2; break;
          case CONFIG_8E2: config = SERIAL_8E2; break;
          case CONFIG_5O1: config = SERIAL_5O1; break;
          case CONFIG_6O1: config = SERIAL_6O1; break;
          case CONFIG_7O1: config = SERIAL_7O1; break;
          case CONFIG_8O1: config = SERIAL_8O1; break;
          case CONFIG_5O2: config = SERIAL_5O2; break;
          case CONFIG_6O2: config = SERIAL_6O2; break;
          case CONFIG_7O2: config = SERIAL_7O2; break;
          case CONFIG_8O2: config = SERIAL_8O2; break;

          default:
            config = SERIAL_8N1; // Default to 8N1 if invalid config value provided
        }
        mbusSerial.end();
        mbusSerial.begin( baud, (mbus_serial_config_t)config );
        wdt_reset();
      }
      break;
    
    case REG_MBUS_SERIAL_DATA:
      while( Wire.available() ) {
        wdt_reset();
        mbusSerial.write( Wire.read() );
      }
      break;
    
    case REG_IRQ:
      //rIRQ = Wire.read();
      wdt_reset();
      break;
    
    default:
      // Ignore writes to unimplemented/invalid registers
      Wire.read(); // Still need to read the data byte(s) to clear them from the I2C buffer, even if we ignore them
      break;
  }

  if( (rSystemFlags & DEBUG_FLAG_I2C_TRAFFIC) == DEBUG_FLAG_I2C_TRAFFIC )
    setDebug( 0, LOW );

  rSequence++;
  if( rSequence == 0x00 )
    rSequence = 0x01; // Ensure sequence number never returns to 0 to avoid confusion with power-on state
}