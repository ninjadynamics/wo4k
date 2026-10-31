#ifndef CONTROLLER_WIRE_H
#define CONTROLLER_WIRE_H
typedef struct ControllerWire {
    volatile LONG sequence;
    DWORD magic,timestamp,connected,buttons;
    LONG x,y,left,right;
} ControllerWire;
#define CONTROLLER_MAGIC 0x574f5044u
#endif
