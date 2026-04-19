#include "queue.h"

KeyEvents kev = {.q = {0},.head = 0,.tail = 0 };

uint16_t key_pop()
{
    // Queue is empty
    while(kev.head == kev.tail)
    {
        sleep_ms(10);
    }
    uint16_t value = kev.q[kev.tail];
    kev.tail =(kev.tail + 1) % 32;
    return value;
}
void key_push(uint16_t value)
{
    if((kev.head + 1) % 32 ==kev.tail)
    {
        return; 
    }
    kev.q[kev.head] = value;
    kev.head = (kev.head + 1) % 32;
}