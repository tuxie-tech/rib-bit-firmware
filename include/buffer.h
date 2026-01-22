#pragma once
#include <Arduino.h>

void buffer_init( uint8_t * buffer, size_t size );
uint8_t buffer_push( uint8_t * buffer, size_t size, uint8_t data );
uint8_t buffer_pop( uint8_t * buffer, size_t size );
uint8_t buffer_peek( uint8_t * buffer, size_t size );
size_t buffer_size( uint8_t * buffer, size_t size );
bool buffer_available( uint8_t * buffer, size_t size );
bool buffer_full( uint8_t * buffer, size_t size );