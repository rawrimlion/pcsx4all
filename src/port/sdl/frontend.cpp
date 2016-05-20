#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "port.h"
#include "r3000a.h"
#include "plugins.h"
#include "profiler.h"
#include <SDL.h>

/* PATH_MAX inclusion */
#ifdef __MINGW32__
#include <limits.h>
#endif

#define timer_delay(a)	wait_ticks(a*1000)

extern int saveslot;
#ifdef gpu_unai
extern bool show_fps;
extern bool frameLimit;
#endif

enum  {
	KEY_UP=0x1,	KEY_LEFT=0x4,		KEY_DOWN=0x10,	KEY_RIGHT=0x40,
	KEY_START=1<<8,	KEY_SELECT=1<<9,	KEY_L=1<<10,	KEY_R=1<<11,
	KEY_A=1<<12,	KEY_B=1<<13,		KEY_X=1<<14,	KEY_Y=1<<15,
};

unsigned int key_read(void)
{
	SDL_Event event;
	static u32 ret = 0;

	while (SDL_PollEvent(&event))  {
		switch (event.type) {
		case SDL_KEYDOWN:
			switch (event.key.keysym.sym) {
			case SDLK_UP:		ret |= KEY_UP;    break;
			case SDLK_DOWN:		ret |= KEY_DOWN;  break;
			case SDLK_LEFT:		ret |= KEY_LEFT;  break;
			case SDLK_RIGHT:	ret |= KEY_RIGHT; break;

			case SDLK_LCTRL:	ret |= KEY_A; break;
			case SDLK_LALT:		ret |= KEY_B; break;
			case SDLK_SPACE:	ret |= KEY_X; break;
			case SDLK_LSHIFT:	ret |= KEY_Y; break;

			case SDLK_TAB:		ret |= KEY_L; break;
			case SDLK_BACKSPACE:	ret |= KEY_R; break;

			case SDLK_RETURN:	ret |= KEY_START; break;
			case SDLK_ESCAPE:	ret |= KEY_SELECT; break;
			case SDLK_m:		ret |= KEY_SELECT | KEY_Y; break;

			default: break;
			}
			break;
		case SDL_KEYUP:
			switch(event.key.keysym.sym) {
			case SDLK_UP:		ret &= ~KEY_UP;    break;
			case SDLK_DOWN:		ret &= ~KEY_DOWN;  break;
			case SDLK_LEFT:		ret &= ~KEY_LEFT;  break;
			case SDLK_RIGHT:	ret &= ~KEY_RIGHT; break;

			case SDLK_LCTRL:	ret &= ~KEY_A; break;
			case SDLK_LALT:		ret &= ~KEY_B; break;
			case SDLK_SPACE:	ret &= ~KEY_X; break;
			case SDLK_LSHIFT:	ret &= ~KEY_Y; break;

			case SDLK_TAB:		ret &= ~KEY_L; break;
			case SDLK_BACKSPACE:	ret &= ~KEY_R; break;

			case SDLK_RETURN:	ret &= ~KEY_START; break;
			case SDLK_ESCAPE:	ret &= ~KEY_SELECT; break;
			case SDLK_m:		ret &= ~(KEY_SELECT | KEY_Y); break;

			default: break;
			}
			break;
		default: break;
		}
	}


	return ret;
}

struct dir_item {
	char	*name;
	s32	type; // 0=dir, 1=file, 2=zip archive
};

void sort_dir(struct dir_item *list, int num_items, int sepdir)
{
	s32 i;
	struct dir_item temp;

	for (i = 0; i < (num_items - 1); i++) {
		if (strcmp(list[i].name, list[i + 1].name) > 0) {
			temp = list[i];
			list[i] = list[i + 1];
			list[i + 1] = temp;
			i = 0;
		}
	}
	if (sepdir) {
		for (i = 0; i < (num_items - 1); i++) {
			if ((list[i].type != 0) && (list[i + 1].type == 0)) {
				temp = list[i];
				list[i] = list[i + 1];
				list[i + 1] = temp;
				i = 0;
			}
		}
	}
}

static char gamepath[PATH_MAX] = "./";
static struct dir_item filereq_dir_items[1024] = { { 0, 0 }, };

#define MENU_X		8
#define MENU_Y		90
#define MENU_LS		(MENU_Y + 10)
#define MENU_HEIGHT	13

static char *GetCwd(void)
{
	getcwd(gamepath, PATH_MAX);
#ifdef __WIN32__
		for (int i = 0; i < PATH_MAX; i++) {
			if (gamepath[i] == 0)
				break;
			if (gamepath[i] == '\\')
				gamepath[i] = '/';
		}
#endif
	return gamepath;
}

#define FREE_LIST() \
do { \
	for (int i = 0; i < num_items; i++) \
		if (filereq_dir_items[i].name) { \
			free(filereq_dir_items[i].name); \
			filereq_dir_items[i].name = NULL; \
		} \
	num_items = 0; \
} while (0)

static char *wildcards[] = {"iso", "bin", "img", "mdf", NULL };

static s32 check_ext(const char *name)
{
	int len = strlen(name);

	if (len < 4)
		return 0;

	if (name[len-4] != '.')
		return 0;

	for (int i = 0; wildcards[i] != NULL; i++) {
		if (strcasecmp(wildcards[i], &name[len-3]) == 0)
			return 1;
	}

	return 0;
}

static s32 get_entry_type(char *cwd, char *d_name)
{
	s32 type;
	struct stat item;
	char *path = (char *)malloc(strlen(cwd) + strlen(d_name) + 2);

	sprintf(path, "%s/%s", cwd, d_name);
	if (!stat(path, &item)) {
		if (S_ISDIR(item.st_mode)) {
			type = 0;
		} else {
			type = 1;
		}
	} else {
		type = 1;
	}

	free(path);
	return type;
}

char *FileReq(char *dir, const char *ext, char *result)
{
	static char *cwd = NULL;
	static s32 cursor_pos = 1;
	static s32 first_visible;
	static s32 num_items = 0;
	DIR *dirstream;
	struct dirent *direntry;
	static s32 row;
	char tmp_string[32];
	u32 keys;

	if (dir)
		chdir(dir);

	if (!cwd) {
		cwd = GetCwd();
	}

	for (;;) {
		keys = key_read();

		video_clear();

		if (keys & KEY_SELECT) {
			FREE_LIST();
			timer_delay(100);
			return NULL;
		}

		if (num_items == 0) {
			dirstream = opendir(cwd);
			if (dirstream == NULL) {
				port_printf(0, 20, "error opening directory");
				return NULL;
			}
			// read directory entries
			while ((direntry = readdir(dirstream))) {
				s32 type = get_entry_type(cwd, direntry->d_name);

				// this is a very ugly way of only accepting a certain extension
				if ((type == 0 && strcmp(direntry->d_name, ".")) ||
				     check_ext(direntry->d_name) ||
				    (ext && (strlen(direntry->d_name) > 4 &&0 == strncasecmp(direntry->d_name + (strlen(direntry->d_name) - strlen(ext)), ext, strlen(ext))))) {
					filereq_dir_items[num_items].name = (char *)malloc(strlen(direntry->d_name) + 1);
					strcpy(filereq_dir_items[num_items].name, direntry->d_name);
					filereq_dir_items[num_items].type = type;
					num_items++;
					if (num_items > 1024) break;
				}
			}
			closedir(dirstream);

			sort_dir(filereq_dir_items, num_items, 1);
			cursor_pos = 0;
			first_visible = 0;
		}

		// display current directory
		port_printf(0, MENU_Y, cwd);

		if (keys & KEY_DOWN) { //down
			if (cursor_pos < (num_items - 1)) cursor_pos++;
			if ((cursor_pos - first_visible) >= MENU_HEIGHT) first_visible++;
		} else if (keys & KEY_UP) { // up
			if (cursor_pos > 0) cursor_pos--;
			if (cursor_pos < first_visible) first_visible--;
		} else if (keys & KEY_LEFT) { //left
			if (cursor_pos >= 10) cursor_pos -= 10;
			else cursor_pos = 0;
			if (cursor_pos < first_visible) first_visible = cursor_pos;
		} else if (keys & KEY_RIGHT) { //right
			if (cursor_pos < (num_items - 11)) cursor_pos += 10;
			else cursor_pos = num_items - 1;
			if ((cursor_pos - first_visible) >= MENU_HEIGHT)
				first_visible = cursor_pos - (MENU_HEIGHT - 1);
		} else if (keys & KEY_A) { // button 1
			// directory selected
			if (filereq_dir_items[cursor_pos].type == 0) {
				strcat(cwd, "/");
				strcat(cwd, filereq_dir_items[cursor_pos].name);

				chdir(cwd);
				cwd = GetCwd();
				FREE_LIST();
			} else {
				sprintf(result, "%s/%s", cwd, filereq_dir_items[cursor_pos].name);

				FREE_LIST();

				video_clear();
				port_printf(0, 120, "ARE YOU SURE YOU WANT TO SELECT...");
				port_printf(0, 130, result);
				port_printf(0, 140, "PRESS START FOR YES OR SELECT FOR NO");
				video_flip();
				// file selected check if it was intended
				for (;; ) {
					u32 keys = key_read();
					if (keys & KEY_SELECT)
						return NULL;
					if (keys & KEY_START) {
						return result;
					}

					timer_delay(100);
				}
			}
		}

		// display directory contents
		row = 0;
		while (row < num_items && row < MENU_HEIGHT) {
			if (row == (cursor_pos - first_visible)) {
				// draw cursor
				port_printf(MENU_X + 16, MENU_LS + (10 * row), "-->");
			}

			if (filereq_dir_items[row + first_visible].type == 0)
				port_printf(MENU_X, MENU_LS + (10 * row), "DIR");
			snprintf(tmp_string, 30, "%s", filereq_dir_items[row + first_visible].name);
			port_printf(MENU_X + (8 * 5), MENU_LS + (10 * row), tmp_string);
			row++;
		}
		while (row < MENU_HEIGHT)
			row++;

		video_flip();
		timer_delay(75);

		if (keys & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R |
			    KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN))
			timer_delay(50);
	}

	return NULL;
}

typedef struct {
	char *name;
	int *par;
	int min, max;
	char **par_name;
	int (*on_press_a)();
} MENUITEM;

typedef struct {
	int num;
	int cur;
	int x, y;
	MENUITEM *m; // array of items
} MENU;

/* Forward declaration */
static int gui_RunMenu(MENU *menu);
static int gui_LoadIso();
static int gui_Settings();
static int gui_Quit();

static MENUITEM gui_MainMenuItems[] = {
	{(char *)"LOAD GAME", NULL, 0, 0, NULL, &gui_LoadIso},
	{(char *)"SETTINGS", NULL, 0, 0, NULL, &gui_Settings},
	{(char *)"LOAD STATE", &saveslot, 0, 9, NULL, NULL},
	{(char *)"SAVE STATE", &saveslot, 0, 9, NULL, NULL},
	{(char *)"QUIT ", NULL, 0, 0, NULL, &gui_Quit},
	{0}
};

#define MENU_SIZE ((sizeof(gui_MainMenuItems) / sizeof(MENUITEM)) - 1)
static MENU gui_MainMenu = { MENU_SIZE, 0, 112, 120, (MENUITEM *)&gui_MainMenuItems };

static char *gui_OffOn[] = {"off", "on"};

static MENUITEM gui_SettingsItems[] = {
#ifdef gpu_unai
	{(char *)"Show FPS            ", (int *)&show_fps, 0, 1, (char **)&gui_OffOn, NULL},
	{(char *)"Frame Limit         ", (int *)&frameLimit, 0, 1, (char **)&gui_OffOn, NULL},
#endif
	{(char *)"Cycle multiplier    ", (int *)&BIAS, 1, 4, NULL, NULL},
	{0}
};

#define SET_SIZE ((sizeof(gui_SettingsItems) / sizeof(MENUITEM)) - 1)
static MENU gui_SettingsMenu = { SET_SIZE, 0, 72, 120, (MENUITEM *)&gui_SettingsItems };

static int gui_LoadIso()
{
	static char isoname[PATH_MAX];
	const char *name = FileReq(NULL, NULL, isoname);

	if (name) {
		SetIsoFile(name);
		return 1;
	}

	return 0;
}

static int gui_Settings()
{
	gui_RunMenu(&gui_SettingsMenu);

	return 0;
}

static int gui_Quit()
{
	pcsx4all_exit();
	return 0;
}

static void ShowMenuItem(int x, int y, MENUITEM *mi)
{
	static char string[PATH_MAX];

	if (mi->name) {
		if (mi->par) {
			if (mi->par_name) {
				sprintf(string, "%s %s", mi->name, mi->par_name[*mi->par]);
			} else {
				sprintf(string, "%s %d", mi->name, *mi->par);
			}
			port_printf(x, y, string);
		} else
			port_printf(x, y, mi->name);
	}
}

static void ShowMenu(MENU *menu)
{
	MENUITEM *mi = menu->m;

	// show menu lines
	for(int i = 0; i < menu->num; i++, mi++) {
		ShowMenuItem(menu->x, menu->y + i * 10, mi);
	}

	// show cursor
	port_printf(menu->x - 3 * 8, menu->y + menu->cur * 10, "-->");

	// general copyrights info
	port_printf( 4 * 8,  0, "pcsx4all 2.3 by Franxis and Chui");
	port_printf( 0 * 8, 10, "based on pcsx-r 1.9 and psx4all-dingoo");
	port_printf( 4 * 8, 20, "mips recompiler by Ulrich Hecht");
	port_printf( 7 * 8, 30, "optimized by Dmitry Smagin");
}

static int gui_RunMenu(MENU *menu)
{
	MENUITEM *mi;
	u32 keys;

	for (;;) {
		mi = menu->m + menu->cur;
		keys = key_read();

		video_clear();

		// check keys
		if (keys & KEY_SELECT) {
			timer_delay(100);
			return 0;
		} else if (keys & KEY_UP) {
			if (--menu->cur < 0)
				menu->cur = menu->num - 1;
		} else if (keys & KEY_DOWN) {
			if (++menu->cur == menu->num)
				menu->cur = 0;
		} else if (keys & KEY_LEFT) {
			if (mi->par && *mi->par > mi->min) *mi->par -= 1;
		} else if (keys & KEY_RIGHT) {
			if (mi->par && *mi->par < mi->max) *mi->par += 1;
		} else if (keys & KEY_A) {
			if (mi->on_press_a) {
				timer_delay(500);
				int result = (*mi->on_press_a)();
				if (result)
					return result;
			}
		}

		// diplay menu
		ShowMenu(menu);

		video_flip();
		timer_delay(75);

		if (keys & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R |
			    KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN))
			timer_delay(50);
	}

	return 0;
}

/* 0 - exit, 1 - game loaded */
int SelectGame()
{
	return gui_RunMenu(&gui_MainMenu);
}