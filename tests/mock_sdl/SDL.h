#pragma once
#include <stdint.h>

#define SDL_TRUE 1
#define SDL_FALSE 0

typedef int SDL_bool;
typedef int64_t SDL_FingerID;
typedef int64_t SDL_TouchID;

struct SDL_TouchFingerEvent {
    uint32_t type;
    uint32_t timestamp;
    SDL_TouchID touchId;
    SDL_FingerID fingerId;
    float x;
    float y;
    float dx;
    float dy;
    float pressure;
};

inline void SDL_GetRelativeMouseState(int *x, int *y) { if(x)*x=0; if(y)*y=0; }
inline void SDL_GetMouseState(int *x, int *y) { if(x)*x=0; if(y)*y=0; }
inline void SDL_SetRelativeMouseMode(int) {}
