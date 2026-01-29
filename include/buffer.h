#pragma once
#include <Arduino.h>

// Simple ring buffer for byte streams.
struct ByteBuffer {
	uint8_t * data;
	size_t size;
	size_t head;
	size_t tail;
	size_t count;
};

void buffer_init( ByteBuffer * buffer, uint8_t * backing, size_t size );
bool buffer_push( ByteBuffer * buffer, uint8_t data );
bool buffer_pop( ByteBuffer * buffer, uint8_t * data );
bool buffer_peek( const ByteBuffer * buffer, uint8_t * data );
size_t buffer_size( const ByteBuffer * buffer );
bool buffer_available( const ByteBuffer * buffer );
bool buffer_full( const ByteBuffer * buffer );