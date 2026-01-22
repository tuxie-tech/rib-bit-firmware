#include "buffer.h"
#include <Arduino.h>

void buffer_init( uint8_t * buffer, size_t size ) {
    for( size_t i = 0; i < size; i++ ) {
        buffer[i] = 0;
    }
}

uint8_t buffer_push( uint8_t * buffer, size_t size, uint8_t data ) {
    // Shift existing data to the left
    for( size_t i = 0; i < size - 1; i++ ) {
        buffer[i] = buffer[i + 1];
    }
    // Add new data at the end
    buffer[size - 1] = data;
    return data;
}

uint8_t buffer_pop( uint8_t * buffer, size_t size ) {
    uint8_t data = buffer[0];
    // Shift existing data to the left
    for( size_t i = 0; i < size - 1; i++ ) {
        buffer[i] = buffer[i + 1];
    }
    buffer[size - 1] = 0; // Clear last element
    return data;
}

uint8_t buffer_peek( uint8_t * buffer, size_t size ) {
    return buffer[0];
}

size_t buffer_size( uint8_t * buffer, size_t size ) {
    size_t count = 0;
    for( size_t i = 0; i < size; i++ ) {
        if( buffer[i] != 0 ) {
            count++;
        } else {
            break; // Stop counting at the first zero byte
        }
    }
    return count;
}

bool buffer_available( uint8_t * buffer, size_t size ) {
    return buffer_size( buffer, size ) > 0;
}

bool buffer_full( uint8_t * buffer, size_t size ) {
    return buffer_size( buffer, size ) == size;
}