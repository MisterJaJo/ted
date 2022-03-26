#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define OFFSET(x) (1 << x)	

// Function and struct definitions
typedef void (*KeybindFunction)();

struct Keybind
{
	const int *keycodes;
	KeybindFunction function;
};

typedef void (*InputModeFallthroughHandler)(int keycode);

struct InputMode
{
	int mode;
	const char *name;
	const struct Keybind *keymap;
	InputModeFallthroughHandler fallthrough;
};

struct Buffer
{
	char **lines;
	unsigned int linescnt;
	int topline;
};

int clampi(int x, int min, int max);
int currline(struct Buffer *buffer);
void delbefore();
void delunder();
void drawui();
void fthandleinput();
void fthandlenormal();
int getcharwidth(wchar_t c);
int getlinewidth(char *s);
int getcurrmode();
void insnewline();
bool ismodeactive(int mode);
struct Buffer *loadbufferfromfile(char *path);
int max(int x, int y);
int min(int x, int y);
void movecursor(int x, int y);
void movebol();
void movedown();
void moveeol();
void moveleft();
void moveright();
void moveup();
void quit();
void quitmode();
void renderbuffer(struct Buffer *buffer);
void setmode(int mode);
void setmodeinsert();
void setmodenormal();

// Include the configuration header file
#include "config.h"

int cursorx = 0;
int cursory = 0;

const int modescount = sizeof(modes) / sizeof(struct InputMode);

struct Buffer *mainbuf = NULL;

int
clampi(int x, int min, int max)
{
	if (x <= min) return min;
	if (x >= max) return max;
	return x;
}

int
currline(struct Buffer *buffer)
{
	return clampi(buffer->topline + cursory, 0, buffer->linescnt - 1);
}

void
delbefore()
{
	movecursor(cursorx - 1, cursory);
	delch();
}

void
delunder()
{
	delch();
}

void 
drawui()
{
	// Print mode string
	char mstr[32];
	int currmodeidx = getcurrmode();
	if (currmodeidx != -1)
	{
		sprintf(mstr, "[ %s mode ]", modes[currmodeidx].name);
		mvprintw(LINES - 1, 0, mstr);
	}

	// Print current buffer information
	if (mainbuf)
	{
		char bstr[64];
		int line = currline(mainbuf);
		char *linestr = mainbuf->lines[line];
		int width = getlinewidth(linestr);
		sprintf(bstr, "Col %d / %d | Line %d / %d", 
				cursorx, width,
				line + 1, mainbuf->linescnt);
		mvprintw(LINES - 1, COLS - 32, bstr);
	}
}

void
fthandleinput(int keycode)
{
	addch(keycode);
	// Move the cursor by the width of the inserted character
	int charwidth = getcharwidth(keycode);
	movecursor(cursorx + charwidth, cursory);
}

void
fthandlenormal(int keycode)
{
}

// Initialize the fake window
static WINDOW *fakewin;

int
getcharwidth(wchar_t c)
{
	// If the fake window was not yet initialized, do it here
	if (fakewin == NULL)
	{
		fakewin = newwin(LINES, COLS, 0, 0);
	}

	// Move to position (0, 0)
	wmove(fakewin, 0, 0);

	// Insert the character
	waddch(fakewin, c);

	// Get new x and y positions
	int newx = 0, newy = 0;
	getyx(fakewin, newy, newx);

	// Return x position
	return newx;
}

int
getlinewidth(char *s)
{
	// If the fake window was not yet initialized, do it here
	if (fakewin == NULL)
	{
		fakewin = newwin(LINES, COLS, 0, 0);
	}

	// Move to position (0, 0)
	wmove(fakewin, 0, 0);

	// Before inserting the string, replace the last character (will be a newline) with a \0 character.
	// This is very hacky and makes this piece of code just beautiful.
	int len = strlen(s);
	char prev = s[len - 2];
	s[len - 2] = '\0';
	waddstr(fakewin, s); // Insert the string
	s[len - 2] = prev;

	// Get new x and y positions
	int newx = 0, newy = 0;
	getyx(fakewin, newy, newx);

	// Return x position
	return newx;
}

int
getcurrmode()
{
	for (int i = 0; i < modescount; ++i)
		if (modes[i].mode == currmode)
			return i;
	return -1;
}

void
insnewline()
{
	addch('\n');
	movecursor(0, cursory + 1);
}

bool
ismodeactive(int mode)
{
	return currmode == mode;
}

struct Buffer *
loadbufferfromfile(char *path)
{
	FILE *fp;
	char *line;
	size_t len = 0;
	ssize_t read;

	// Try to open the given file
	fp = fopen(path, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "Failed reading file '%s'\n", path);
		exit(EXIT_FAILURE);
	}

	// Create the main buffer
	const unsigned int linescapincr = 128;
	unsigned int linescap = linescapincr;
	struct Buffer *buf = (struct Buffer *) malloc(sizeof(struct Buffer));
	buf->linescnt = 0;
	buf->lines = (char **) malloc(linescap * sizeof(char *));
	buf->topline = 0;

	// Read the file
	int lineidx = 0;
	while ((read = getline(&line, &len, fp)) != -1)
	{
		// If the number of lines read exceeds the capacity, reallocate the buffer
		if (buf->linescnt > linescap)
		{
			linescap += linescapincr;
			buf->lines = (char **) realloc(buf->lines, linescap * sizeof(char *));
		}

		// Create a new line entry in the buffer and copy over the
		// line that was read from the file.
		// Ignore the newline at the end of the line.
		buf->lines[lineidx] = malloc(len * sizeof(char));
		buf->linescnt += 1;
		strcpy(buf->lines[lineidx], line);
		lineidx++;
	}

	return buf;
}

int
max(int x, int y)
{
	if (x > y) return x;
	return y;
}

int
min(int x, int y)
{
	if (x < y) return x;
	return y;
}

void
movecursor(int x, int y)
{
	// Reset attributes for previous position
	mvchgat(cursory, cursorx, 1, A_NORMAL, 0, NULL);  

	// Perform vertical window border movement
	int yoff = y - cursory;
	if ((cursory == 0 && yoff < 0) || (cursory == LINES - 2 && yoff > 0))
	{
		mainbuf->topline = clampi(mainbuf->topline + yoff, 0, mainbuf->linescnt - LINES + 1);
	}
	int maxline = min(LINES - 2, mainbuf->linescnt - 1);
	cursory = clampi(y, 0, maxline);

	// Clamp the cursor X coordinate to 0 and the length of the current line
	int mainbufline = currline(mainbuf);
	char *linestr = mainbuf->lines[mainbufline];
	int linewidth = getlinewidth(linestr);
	cursorx = clampi(x, 0, linewidth);

	// Move the cursor
	move(cursory, cursorx);

	// Set new attribute for new position
	mvchgat(cursory, cursorx, 1, A_REVERSE, 0, NULL); 
}

void movebol()   { movecursor(0, cursory); }
void movedown()  { movecursor(cursorx, cursory + 1); }
void moveeol()
{
	int mainbufline = currline(mainbuf);
	char *linestr = mainbuf->lines[mainbufline];
	if (linestr)
	{
		int linewidth = getlinewidth(linestr);
		movecursor(linewidth, cursory);
	}
}
void moveleft()  { movecursor(cursorx - 1, cursory); }
void moveright() { movecursor(cursorx + 1, cursory); }
void moveup()    { movecursor(cursorx, cursory - 1); }

void 
setmode(int mode)
{
	currmode = mode;
}

void
setmodeinsert()
{
	setmode(MODE_INSERT);
}

void
setmodenormal()
{
	setmode(MODE_NORMAL);
}

void 
quit()
{
	// Delete the fake window
	delwin(fakewin);

	endwin();
	exit(EXIT_SUCCESS);
}

void
quitmode()
{
	if (getch() == -1) { 
		setmode(MODE_NORMAL); 
		return; 
	}
}

void
renderbuffer(struct Buffer *buffer)
{
	for (int line = 0; line < LINES - 1 && (buffer->topline + line) < buffer->linescnt; ++line)
	{
		mvprintw(line, 0, buffer->lines[buffer->topline + line]);
	}
	movecursor(cursorx, cursory);
}

void 
run()
{
	int key = 0;
	while (1) 
	{
		// Handle keyboard input
		key = getch();	

		// Move the cursor back
		movecursor(cursorx, cursory);

		if (key != ERR)
		{
			// Get current mode
			int currmodeidx = getcurrmode();
			if (currmodeidx != -1)
			{
				// Iterate current mode's keymap
				const struct Keybind *currkb;
				for (currkb = modes[currmodeidx].keymap; currkb && currkb->keycodes != NULL; ++currkb)
				{
					// Iterate keycodes in the keymap
					const int *currkeycode;
					for (currkeycode = currkb->keycodes; currkeycode && *currkeycode != 0; ++currkeycode)
					{
						// If the current keycode matches the pressed key...
						if (*currkeycode == key)
						{
							// Run the keybind function if it is valid.
							if (currkb->function)
							{
								(*currkb->function)();
								goto break_loop;
							}
						}
					}
				}

				// If no key from the keymap was pressed,
				// call the fallthrough handler function.
				(*modes[currmodeidx].fallthrough)(key);

break_loop:
				;
			}
		}

		// Render the currently loaded buffer
		renderbuffer(mainbuf);

		// Draw UI
		drawui();

		// Refresh
		refresh();

		// Sleep for 15ms to not hog the CPU
		napms(15);
	}
}

int
main(int argc, char *argv[])
{
	// Load the file
	if (argc >= 2)
	{
		char *path = argv[1];
		mainbuf = loadbufferfromfile(path);
	}

	setlocale(locale, "");

	// Initialize ncurses
	initscr();
	noecho();
	cbreak();
	keypad(stdscr, TRUE);

	// Set the ESCDELAY global variable to 0 to remove the delay after the Escape
	// key is pressed.
	set_escdelay(0);

	// Hide the default screen cursor
	curs_set(0);

	// Make getch a non-blocking call
	nodelay(stdscr, TRUE);

	// Run ted
	run();

	// End
	quit();
}
