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

#define timer_delay(a)	wait_ticks(a*1000)

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

char gamepath[256] = ".";
static char filereq_fullgamepath[257];
static struct dir_item filereq_dir_items[1024] = { { 0, 0 }, };

#define MENU_X		8
#define MENU_Y		90
#define MENU_LS		(MENU_Y + 10)
#define MENU_HEIGHT	13

char *FileReq(char *dir, const char *ext)
{
	static char *cwd = NULL;
	static s32 cursor_pos = 1;
	static s32 first_visible;
	static s32 num_items = 0;
	DIR *dirstream;
	struct dirent *direntry;
	char *path;
	struct stat item;
	static s32 row;
	s32 pathlength;
	char tmp_string[32];
	char *selected;
	u32 keys;

	if (dir != NULL)
		cwd = dir;

	if (cwd == NULL) {
		getcwd(gamepath, 256);
#ifdef __WIN32__
		for (int i = 0; i < 256; i++) {
			if (gamepath[i] == 0)
				break;
			if (gamepath[i] == '\\')
				gamepath[i] = '/';
		}
#endif
		sprintf(filereq_fullgamepath, "%s/", gamepath);
		cwd = filereq_fullgamepath;
	}

	for (;;) {
		keys = key_read();

		video_clear();

		if (keys & KEY_SELECT) {
			for (int i = 0; i < num_items; i++) if (filereq_dir_items[i].name) {
					free(filereq_dir_items[i].name); filereq_dir_items[i].name = NULL;
				}
			num_items = 0;
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
				// this is a very ugly way of only accepting a certain extension
				if ((ext == NULL &&
				     ((NULL == strstr(direntry->d_name, ".")) ||
				      (strlen(direntry->d_name) > 1 && 0 == strnicmp(direntry->d_name, "..", 2)) ||
				      (strlen(direntry->d_name) > 2 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - 2), ".z", 2)) ||
				      (strlen(direntry->d_name) > 4 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - 4), ".iso", 4)) ||
				      (strlen(direntry->d_name) > 4 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - 4), ".bin", 4)) ||
				      (strlen(direntry->d_name) > 4 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - 4), ".img", 4)) ||
				      (strlen(direntry->d_name) > 4 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - 4), ".znx", 4)) ||
				      (strlen(direntry->d_name) > 4 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - 4), ".cbn", 4)))) ||
				    (ext != NULL && (strlen(direntry->d_name) > 4 && 0 == strnicmp(direntry->d_name + (strlen(direntry->d_name) - strlen(ext)), ext, strlen(ext))))) {
					filereq_dir_items[num_items].name = (char *)malloc(strlen(direntry->d_name) + 1);
					strcpy(filereq_dir_items[num_items].name, direntry->d_name);
					num_items++;
					if (num_items > 1024) break;
				}
			}
			closedir(dirstream);
			// get entry types
			for (int i = 0; i < num_items; i++) {
				path = (char *)malloc(strlen(cwd) + strlen(filereq_dir_items[i].name) + 2);
				sprintf(path, "%s/%s", cwd, filereq_dir_items[i].name);
				if (!stat(path, &item)) {
					if (S_ISDIR(item.st_mode)) {
						filereq_dir_items[i].type = 0;
					} else {
						s32 len = strlen(filereq_dir_items[i].name);

						filereq_dir_items[i].type = 2;
						/* Not Used */
						if (len >= 4) {
							if (!strnicmp(filereq_dir_items[i].name + (len - 2), ".Z", 2))
								filereq_dir_items[i].type = 1;
							if (!strnicmp(filereq_dir_items[i].name + (len - 4), ".bin", 4))
								filereq_dir_items[i].type = 1;
							if (!strnicmp(filereq_dir_items[i].name + (len - 4), ".ZNX", 4))
								filereq_dir_items[i].type = 1;
						}
					}
				} else {
					filereq_dir_items[i].type = 0;
				}
				free(path);
			}
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
			path = (char *)malloc(strlen(cwd)
					    + strlen(filereq_dir_items[cursor_pos].name)
					    + 2);
			sprintf(path, "%s/%s", cwd, filereq_dir_items[cursor_pos].name);
			for (int i = 0; i < num_items; i++) if (filereq_dir_items[i].name) {
					free(filereq_dir_items[i].name); filereq_dir_items[i].name = NULL;
				}
			num_items = 0;
			if (filereq_dir_items[cursor_pos].type == 0) {
				// directory selected
				pathlength = strlen(path);
				if (path[pathlength - 1] == '.' &&
				    path[pathlength - 2] == '/') { // check for . selected
					path[pathlength - 2] = '\0';
					cwd = path;
				} else if (path[pathlength - 1] == '.'
					   && path[pathlength - 2] == '.'
					   && path[pathlength - 3] == '/') { // check for .. selected
					if (pathlength > 4) {
						char *p = strrchr(path, '/');     // PATH: /x/y/z/..[/]
						p[0] = '\0';
						p = strrchr(path, '/');         // PATH: /x/y/z[/]../
						p[0] = '\0';
						p = strrchr(path, '/');         // PATH: /x/y[/]z/../
						p[1] = '\0';                    // PATH: /x/y/

						cwd = path;
					}
				} else {
					// dirty fix
					if (path[0] == '/' &&
					    path[1] == '/')
						cwd = path + 1; // Add 1 to ignore the first slash. This occurs when traversing to root dir.
					else
						cwd = path;
				}
			} else {
				video_clear();
				port_printf(0, 120, "ARE YOU SURE YOU WANT TO SELECT...");
				port_printf(0, 130, path);
				port_printf(0, 140, "PRESS START FOR YES OR SELECT FOR NO");
				video_flip();
				// file selected check if it was intended
				for (;; ) {
					u32 keys = key_read();
					if (keys & KEY_SELECT)
						return NULL;
					if (keys & KEY_START) {
						/* Store the 10 character filename in CdromLabel so save states work */
						char *p = strrchr(path, '/');
						if (p != NULL)
							sprintf(CdromLabel, "%10.10s", p + 1);
						return path;
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

				selected = filereq_dir_items[row + first_visible].name;
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
