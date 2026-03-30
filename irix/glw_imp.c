#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "../ref_gl/gl_local.h"
#include "../client/keys.h"
#include "../linux/rw_linux.h"

#include "minigl_irix.h"

extern cvar_t *vid_fullscreen;

typedef struct {
	int width;
	int height;
	GrScreenResolution_t resolution;
} glide_mode_t;

static const glide_mode_t glide_modes[] = {
	{ 320, 240, GR_RESOLUTION_320x240 },
	{ 400, 300, GR_RESOLUTION_400x300 },
	{ 512, 384, GR_RESOLUTION_512x384 },
	{ 640, 400, GR_RESOLUTION_640x400 },
	{ 640, 480, GR_RESOLUTION_640x480 },
	{ 800, 600, GR_RESOLUTION_800x600 },
	{ 960, 720, GR_RESOLUTION_960x720 },
	{ 1024, 768, GR_RESOLUTION_1024x768 },
	{ 1280, 1024, GR_RESOLUTION_1280x1024 },
	{ 1600, 1200, GR_RESOLUTION_1600x1200 }
};

static in_state_t *glw_in_state;
static Key_Event_fp_t glw_key_event_fp;
static int glw_stdin_fd = 0;
static int glw_stdin_flags = -1;
static qboolean glw_terminal_active;
static qboolean glw_terminal_available;
static struct termios glw_terminal_mode;
static unsigned char glw_input_buffer[64];
static int glw_input_length;
static unsigned glw_escape_time;

#ifndef O_NONBLOCK
#define O_NONBLOCK FNDELAY
#endif

#define GLW_ESCAPE_TIMEOUT_MS 75u

static void signal_handler(int sig)
{
	fprintf(stderr, "Received signal %d, shutting down miniGL.\n", sig);
	GLimp_Shutdown();
	_exit(0);
}

static void InitSig(void)
{
	struct sigaction sa;

	sigaction(SIGINT, 0, &sa);
	sa.sa_handler = signal_handler;
	sigaction(SIGINT, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
}

static qboolean GLimp_FindResolution(int width, int height, GrScreenResolution_t *resolution)
{
	unsigned i;

	for (i = 0; i < (sizeof(glide_modes) / sizeof(glide_modes[0])); ++i)
	{
		if (glide_modes[i].width == width && glide_modes[i].height == height)
		{
			*resolution = glide_modes[i].resolution;
			return true;
		}
	}

	return false;
}

static void GLW_TerminalRestore(void)
{
	if (glw_terminal_active)
	{
		tcsetattr(glw_stdin_fd, TCSANOW, &glw_terminal_mode);
		if (glw_stdin_flags != -1)
			fcntl(glw_stdin_fd, F_SETFL, glw_stdin_flags);
	}

	glw_terminal_active = false;
	glw_input_length = 0;
	glw_escape_time = 0;
}

static qboolean GLW_TerminalInit(void)
{
	struct termios raw_mode;

	glw_terminal_available = false;
	glw_terminal_active = false;
	glw_input_length = 0;
	glw_escape_time = 0;

	if (!isatty(glw_stdin_fd))
		return false;

	if (tcgetattr(glw_stdin_fd, &glw_terminal_mode) == -1)
		return false;

	raw_mode = glw_terminal_mode;
	raw_mode.c_lflag &= ~(ICANON | ECHO);
	raw_mode.c_iflag &= ~(ICRNL | IXON);
	raw_mode.c_cc[VMIN] = 0;
	raw_mode.c_cc[VTIME] = 0;

	glw_stdin_flags = fcntl(glw_stdin_fd, F_GETFL, 0);
	if (glw_stdin_flags == -1)
		glw_stdin_flags = 0;

	if (tcsetattr(glw_stdin_fd, TCSANOW, &raw_mode) == -1)
		return false;

	if (fcntl(glw_stdin_fd, F_SETFL, glw_stdin_flags | O_NONBLOCK) == -1)
	{
		tcsetattr(glw_stdin_fd, TCSANOW, &glw_terminal_mode);
		return false;
	}

	glw_terminal_available = true;
	glw_terminal_active = true;
	return true;
}

static void GLW_SendKey(int key)
{
	if (!glw_key_event_fp || key <= 0 || key >= 256)
		return;

	glw_key_event_fp(key, true);
	glw_key_event_fp(key, false);
}

static int GLW_MapAsciiKey(int ch)
{
	switch (ch)
	{
	case '\r':
	case '\n':
		return K_ENTER;
	case '\t':
		return K_TAB;
	case 8:
	case 127:
		return K_BACKSPACE;
	default:
		if (ch >= 32 && ch <= 126)
		{
			if (isupper(ch))
				ch = tolower(ch);
			return ch;
		}
		break;
	}

	return 0;
}

static int GLW_ParseEscapeSequence(const unsigned char *buffer, int length, int *key, int *consumed)
{
	if (!buffer || length <= 0 || buffer[0] != 27)
		return 0;

	if (length == 1)
		return -1;

	if (length >= 3 && (buffer[1] == '[' || buffer[1] == 'O'))
	{
		switch (buffer[2])
		{
		case 'A': *key = K_UPARROW;    *consumed = 3; return 1;
		case 'B': *key = K_DOWNARROW;  *consumed = 3; return 1;
		case 'C': *key = K_RIGHTARROW; *consumed = 3; return 1;
		case 'D': *key = K_LEFTARROW;  *consumed = 3; return 1;
		case 'H': *key = K_HOME;       *consumed = 3; return 1;
		case 'F': *key = K_END;        *consumed = 3; return 1;
		default:
			break;
		}
	}
	else if (length == 2 && (buffer[1] == '[' || buffer[1] == 'O'))
	{
		return -1;
	}

	if (length >= 4 && buffer[1] == '[' && buffer[3] == '~')
	{
		switch (buffer[2])
		{
		case '1': *key = K_HOME; *consumed = 4; return 1;
		case '2': *key = K_INS;  *consumed = 4; return 1;
		case '3': *key = K_DEL;  *consumed = 4; return 1;
		case '4': *key = K_END;  *consumed = 4; return 1;
		case '5': *key = K_PGUP; *consumed = 4; return 1;
		case '6': *key = K_PGDN; *consumed = 4; return 1;
		default:
			break;
		}
	}
	else if (length == 3 && buffer[1] == '[' && buffer[2] >= '0' && buffer[2] <= '9')
	{
		return -1;
	}

	return 0;
}

static void GLW_ProcessInputBuffer(void)
{
	int offset = 0;

	while (offset < glw_input_length)
	{
		int key = 0;
		int consumed = 0;
		int parse_result;

		if (glw_input_buffer[offset] != 27)
		{
			key = GLW_MapAsciiKey(glw_input_buffer[offset]);
			if (key)
				GLW_SendKey(key);
			offset++;
			continue;
		}

		parse_result = GLW_ParseEscapeSequence(glw_input_buffer + offset, glw_input_length - offset, &key, &consumed);
		if (parse_result > 0)
		{
			GLW_SendKey(key);
			offset += consumed;
			continue;
		}

		if (parse_result < 0 && ((unsigned)Sys_Milliseconds() - glw_escape_time) < GLW_ESCAPE_TIMEOUT_MS)
			break;

		GLW_SendKey(K_ESCAPE);
		offset++;
	}

	if (offset > 0)
	{
		glw_input_length -= offset;
		if (glw_input_length > 0)
			memmove(glw_input_buffer, glw_input_buffer + offset, glw_input_length);
	}

	if (glw_input_length > 0 && glw_input_buffer[0] == 27 && glw_escape_time == 0)
		glw_escape_time = (unsigned)Sys_Milliseconds();
	else if (glw_input_length == 0)
		glw_escape_time = 0;
}

int GLimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
	int width;
	int height;
	GrScreenResolution_t resolution;

	ri.Con_Printf(PRINT_ALL, "Initializing direct Glide display\n");
	ri.Con_Printf(PRINT_ALL, "...setting mode %d:", mode);

	if (!ri.Vid_GetModeInfo(&width, &height, mode))
	{
		ri.Con_Printf(PRINT_ALL, " invalid mode\n");
		return rserr_invalid_mode;
	}

	if (!GLimp_FindResolution(width, height, &resolution))
	{
		ri.Con_Printf(PRINT_ALL, " %d %d not supported by the miniGL path\n", width, height);
		return rserr_invalid_mode;
	}

	if (!fullscreen)
		ri.Con_Printf(PRINT_ALL, "...miniGL/Glide is fullscreen-only; ignoring windowed request\n");

	GLimp_Shutdown();

	if (!MiniGL_SetMode(resolution, width, height))
	{
		ri.Con_Printf(PRINT_ALL, " Glide open failed for %d x %d\n", width, height);
		return rserr_invalid_mode;
	}

	*pwidth = width;
	*pheight = height;
	ri.Vid_NewWindow(width, height);

	ri.Con_Printf(PRINT_ALL, " %d %d\n", width, height);
	return rserr_ok;
}

void GLimp_Shutdown(void)
{
	GLW_TerminalRestore();
	MiniGL_Shutdown();
}

int GLimp_Init(void *hinstance, void *wndproc)
{
	(void)hinstance;
	(void)wndproc;

	InitSig();
	return true;
}

void GLimp_BeginFrame(float camera_separation)
{
	(void)camera_separation;
}

void GLimp_EndFrame(void)
{
	MiniGL_SwapBuffers();
}

void GLimp_AppActivate(qboolean active)
{
	(void)active;
}

static void RW_IN_ForceCenterView_f(void)
{
	if (glw_in_state)
		glw_in_state->viewangles[PITCH] = 0;
}

static void RW_IN_MLookDown(void)
{
}

static void RW_IN_MLookUp(void)
{
	if (glw_in_state)
		glw_in_state->IN_CenterView_fp();
}

void KBD_Init(Key_Event_fp_t fp)
{
	glw_key_event_fp = fp;
	GLW_TerminalInit();
}

void KBD_Update(void)
{
	unsigned char read_buffer[32];
	int bytes_read;

	if (!glw_terminal_active)
		return;

	for (;;)
	{
		bytes_read = read(glw_stdin_fd, read_buffer, sizeof(read_buffer));
		if (bytes_read <= 0)
		{
			if (bytes_read == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
				break;

			GLW_TerminalRestore();
			return;
		}

		if ((glw_input_length + bytes_read) > (int)sizeof(glw_input_buffer))
		{
			glw_input_length = 0;
			glw_escape_time = 0;
		}

		memcpy(glw_input_buffer + glw_input_length, read_buffer, bytes_read);
		if (glw_input_length == 0 && read_buffer[0] == 27)
			glw_escape_time = (unsigned)Sys_Milliseconds();
		glw_input_length += bytes_read;
	}

	GLW_ProcessInputBuffer();
}

void KBD_Close(void)
{
	GLW_TerminalRestore();
	glw_key_event_fp = NULL;
}

void RW_IN_Init(in_state_t *in_state_p)
{
	glw_in_state = in_state_p;

	ri.Cvar_Get("_windowed_mouse", "0", CVAR_ARCHIVE);
	ri.Cvar_Get("in_mouse", "0", CVAR_ARCHIVE);
	ri.Cvar_Get("m_filter", "0", 0);
	ri.Cvar_Get("freelook", "1", 0);
	ri.Cvar_Get("lookstrafe", "0", 0);
	ri.Cvar_Get("sensitivity", "3", 0);
	ri.Cvar_Get("m_pitch", "0.022", 0);
	ri.Cvar_Get("m_yaw", "0.022", 0);
	ri.Cvar_Get("m_forward", "1", 0);
	ri.Cvar_Get("m_side", "0.8", 0);

	ri.Cmd_AddCommand("+mlook", RW_IN_MLookDown);
	ri.Cmd_AddCommand("-mlook", RW_IN_MLookUp);
	ri.Cmd_AddCommand("force_centerview", RW_IN_ForceCenterView_f);

	if (glw_terminal_available)
		ri.Con_Printf(PRINT_ALL, "miniGL input: terminal keyboard mode active on stdin (Esc, `, arrows, Enter, Backspace, text).\n");
	else
		ri.Con_Printf(PRINT_ALL, "miniGL input note: stdin is not a terminal; keyboard emulation is unavailable.\n");
}

void RW_IN_Shutdown(void)
{
	ri.Cmd_RemoveCommand("+mlook");
	ri.Cmd_RemoveCommand("-mlook");
	ri.Cmd_RemoveCommand("force_centerview");
	glw_in_state = NULL;
}

void RW_IN_Commands(void)
{
}

void RW_IN_Move(usercmd_t *cmd)
{
	(void)cmd;
}

void RW_IN_Frame(void)
{
}

void RW_IN_Activate(qboolean active)
{
	(void)active;
}
