#include "buffer.h"
#include <Arduino.h>

void buffer_init( ByteBuffer * buffer, uint8_t * backing, size_t size ) {
    buffer->data = backing;
    buffer->size = size;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    for( size_t i = 0; i < size; i++ ) {
        buffer->data[i] = 0;
    }
}

bool buffer_push( ByteBuffer * buffer, uint8_t data ) {
    if( buffer_full( buffer ) )
        return false;

    buffer->data[buffer->head] = data;
    buffer->head = (buffer->head + 1) % buffer->size;
    buffer->count++;
    return true;
}

bool buffer_pop( ByteBuffer * buffer, uint8_t * data ) {
    if( !buffer_available( buffer ) )
        return false;

    if( data != nullptr )
        *data = buffer->data[buffer->tail];
    buffer->tail = (buffer->tail + 1) % buffer->size;
    buffer->count--;
    return true;
}

bool buffer_peek( const ByteBuffer * buffer, uint8_t * data ) {
    if( !buffer_available( buffer ) )
        return false;

    *data = buffer->data[buffer->tail];
    return true;
}

size_t buffer_size( const ByteBuffer * buffer ) {
    return buffer->count;
}

bool buffer_available( const ByteBuffer * buffer ) {
    return buffer->count > 0;
}

bool buffer_full( const ByteBuffer * buffer ) {
    return buffer->count >= buffer->size;
}