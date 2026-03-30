#ifndef MINIGL_IRIX_H
#define MINIGL_IRIX_H

#include "../game/q_shared.h"
#include <glide.h>

qboolean MiniGL_SetMode(GrScreenResolution_t resolution, int width, int height);
void MiniGL_Shutdown(void);
void MiniGL_SwapBuffers(void);

#endif
