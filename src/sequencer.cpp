#include "sequencer.h"

int offset = 0;
int step = 493.883*N / RATE * (1<<16);
uint8_t buffer[1024]; 
unordered_map<int, Track> mp;


int main(int argc, char** argv){
    //checks for bs

    for(;;){
        //when keypressed trigger interrupt and do{
        Track curr = new Track();



    }

    return 0;
}