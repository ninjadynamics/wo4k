#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "controller_wire.h"
_Static_assert(SDL_GAMEPAD_BUTTON_SOUTH==0 && SDL_GAMEPAD_BUTTON_EAST==1 &&
    SDL_GAMEPAD_BUTTON_WEST==2 && SDL_GAMEPAD_BUTTON_NORTH==3 &&
    SDL_GAMEPAD_BUTTON_START==6 && SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER==10 &&
    SDL_GAMEPAD_BUTTON_DPAD_UP==11 && SDL_GAMEPAD_BUTTON_DPAD_RIGHT==14,"SDL button ABI");
int wmain(int argc,wchar_t **argv);
int wmain(int argc,wchar_t **argv) {
    if(argc!=2)return 2;
    DWORD parent=(DWORD)wcstoul(argv[1],0,10);char name[64];
    snprintf(name,sizeof(name),"Local\\WO2097Input-%lu",(unsigned long)parent);
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,parent);
    HANDLE mapping=OpenFileMappingA(FILE_MAP_ALL_ACCESS,FALSE,name);
    if(!process || !mapping)return 3;
    ControllerWire *wire=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(*wire));
    if(!wire)return 4;
    FILE *log=fopen("../logs/controller.log","w");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    if(!SDL_Init(SDL_INIT_GAMEPAD)) {if(log)fprintf(log,"SDL_Init: %s\n",SDL_GetError());return 5;}
    SDL_Gamepad *pad=NULL;
    while(WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        SDL_Event event;while(SDL_PollEvent(&event)) {}
        if(pad && !SDL_GamepadConnected(pad)) {SDL_CloseGamepad(pad);pad=NULL;}
        if(!pad) {
            int count=0;SDL_JoystickID *ids=SDL_GetGamepads(&count);
            for(int i=0;ids && i<count && !pad;++i)pad=SDL_OpenGamepad(ids[i]);
            SDL_free(ids);
            if(pad && log) {fprintf(log,"Connected: %s\n",SDL_GetGamepadName(pad));fflush(log);}
        }
        ControllerWire state={0};state.magic=CONTROLLER_MAGIC;state.timestamp=GetTickCount();state.connected=pad!=NULL;
        if(pad) {
            for(int i=0;i<SDL_GAMEPAD_BUTTON_COUNT && i<32;++i)if(SDL_GetGamepadButton(pad,(SDL_GamepadButton)i))state.buttons|=1u<<i;
            state.x=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX);state.y=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY);
            state.left=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER);state.right=SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        }
        InterlockedIncrement(&wire->sequence);
        wire->magic=state.magic;wire->timestamp=state.timestamp;wire->connected=state.connected;wire->buttons=state.buttons;
        wire->x=state.x;wire->y=state.y;wire->left=state.left;wire->right=state.right;
        InterlockedIncrement(&wire->sequence);Sleep(4);
    }
    if(pad)SDL_CloseGamepad(pad);
    SDL_Quit();if(log)fclose(log);
    UnmapViewOfFile(wire);CloseHandle(mapping);CloseHandle(process);return 0;
}
