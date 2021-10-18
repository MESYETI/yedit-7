#ifdef _WIN32
	#error yedit does not support windows
#endif
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <ncurses.h>
#include <unistd.h>
#include <inicxx.hh>
#include "constants.hh"
#include "editmode.hh"
#include "fs.hh"
#include "colour.hh"
#include "util.hh"
#include "ui.hh"
#include "win.hh"
#include "txtbar.hh"
#include "ui.hh"
using std::vector;
using std::string;
using std::ofstream;
using std::ifstream;
using std::to_string;
using std::endl;

#define ctrl(x)    ((x) & 0x1f)

bool   alert = false;
string alertContent;
int16_t alertDuration;

bool   boxin = false;
string boxinTitle;

void showAlert(string alertc) {
	alert = true;
	alertContent = alertc;
	alertDuration = 3000; // alertDuration is in milliseconds
}

uint64_t countLines(string buf) {
	uint64_t ret = 0;
	for (uint64_t i = 0; i<buf.length(); ++i) {
		if (buf[i] == 10)
			++ ret;
	}
	return ret;
}

void createDefaultConfig() {
	pcreate((std::string)getenv("HOME") +"/.config/yedit");
	ofstream fhnd;
	fhnd.open((std::string) getenv("HOME") + "/.config/yedit/yedit.ini");
	if (true) {
		fhnd << "[appearence]\n"
				"; colour guide\n"
				"; 0 = black\n"
				"; 1 = red\n"
				"; 2 = green\n"
				"; 3 = yellow\n"
				"; 4 = blue\n"
				"; 5 = magenta\n"
				"; 6 = cyan\n"
				"; 7 = white\n"
				"editor_b=4\n"
				"editor_f=7\n"
				"titlebar_b=7\n"
				"titlebar_f=0\n"
				"alert_b=2\n"
				"alert_f=0\n"
				"time_b=4\n"
				"time_f=7\n"
				"\n"
				"[editor]\n"
				"tab-width=4\n";
		fhnd.close();
	}
}

int main(int argc, const char* argv[]) {
	string   fname              = "Unnamed";
	char     fnamec;
	string   fbuf;
	uint16_t maxx = 0, maxy = 0;
	bool     run                = true; // condition for run loop
	bool     renderCurs;
	uint64_t curp               = 0;    // cursor position in file buffer
	uint16_t curx               = 0, cury = 0;
	uint16_t in;                        // input is temporarily stored here
	string   instr;
	uint64_t scrollY            = 0;
	uint64_t lines, cols;
	ofstream ofile;
	editMode emode              = mode_txt;
	uint8_t  tabWidth           = 4;
	bool     syntaxHighlighting = false;
	bool     inString           = false;
	string   temp;
	bool     renderedCursor     = false;
	bool     renderHelpMenu     = false;
	size_t   frames             = 0;
	bool     dialogInFocus      = false;

	vector <string> args; // command line arguments
	for (uint16_t i = 0; i<argc; ++i) {
		args.push_back(argv[i]); // convert argv into std::vector<std::string>
	}

	// process args
	for (uint16_t i = 1; i<args.size(); ++i) {
		if (args[i][0] != '-') {
			fname = args[i];
			fbuf  = fread(fname);
		}
		else {
			if (args[i] == "--version") {
				printf(APP_NAME "\n");
				return 0;
			}
			else if (args[i] == "--restoreconfig") {
				printf("Restoring..");
				createDefaultConfig();
				printf("\rRestored config to %s/.config/yedit/yedit.ini\n", getenv("HOME"));
				exit(0);
			}
			else if (args[i] == "--help") {
				printf(APP_NAME " help\n");
				for (uint8_t i = 0; i<=strlen(APP_NAME "help"); ++i) {
					putchar('=');
				}
				putchar(10);
				printf("Control Q: Quit yedit\nControl S: Save file buffer\nControl H: Enable experimental syntax highlighting\n");
				return 0;
			}
		}
	}

	// theme
	uint8_t editor_back;
	uint8_t editor_fore;
	uint8_t titlebar_back;
	uint8_t titlebar_fore;
	uint8_t alert_fore;
	uint8_t alert_back;
	uint8_t time_fore;
	uint8_t time_back;
	uint8_t win_back;
	uint8_t win_fore;
	uint8_t win_close;
	uint8_t h_int;
	uint8_t h_str;

	INI::Structure settings;

	string err;

	// settings
	if (o_fexists((std::string) getenv("HOME") + "/.config/yedit/yedit.ini")) {
		try {
			settings.Parse(fread((std::string) getenv("HOME") +"/.config/yedit/yedit.ini"));
		} catch (const INI::ParserException& Error) {
			printf("Broken yedit.ini\n");
			exit(2);
		}
		if (!settingsExist(settings)) {
			printf("Non-existant required properties in yedit.ini\n");
			exit(1);
		}
		editor_back   = settings.AsInteger("appearence", "editor_b");
		editor_fore   = settings.AsInteger("appearence", "editor_f");
		titlebar_back = settings.AsInteger("appearence", "titlebar_b");
		titlebar_fore = settings.AsInteger("appearence", "titlebar_f");
		alert_back    = settings.AsInteger("appearence", "alert_b");
		alert_fore    = settings.AsInteger("appearence", "alert_f");
		time_back     = settings.AsInteger("appearence", "time_b");
		time_fore     = settings.AsInteger("appearence", "time_f");
		tabWidth      = settings.AsInteger("editor", "tab-width");
	}
	else {
		createDefaultConfig();
		editor_back   = COLOR_BLUE;
		editor_fore   = COLOR_WHITE;
		titlebar_back = COLOR_WHITE;
		titlebar_fore = COLOR_BLACK;
		alert_fore    = COLOR_BLACK;
		alert_back    = COLOR_GREEN;
		tabWidth      = 4;
	}
	win_back      = COLOR_WHITE;
	win_fore      = COLOR_BLACK;
	win_close     = COLOR_RED;
	h_int         = COLOR_CYAN;
	h_str         = COLOR_GREEN;

	// make windows
	ui_window helpMenu; // = newWindow(0, 0, 20, 7, "help");
	helpMenu.create(0, 0, 20, 8, "help");
	helpMenu.print("yedit keybinds\ncontrol s: save\ncontrol q: quit\ncontrol g:\nsyntax highlighting\ncontrol r: refresh\nconfig");

	ui_window dialogMenu;
	dialogMenu.create(0, 0, 40, 3, "open file");
	dialogMenu.print("type in the name of the file");

	ui_textbar dialog;
	dialog.create(1, 2, 39);

	initscr();
	start_color();
	raw();
	use_default_colors();
	keypad(stdscr, true);
	curs_set(0);

	if (!has_colors()) {
		printw("yedit requires a terminal that supports colour\npress any key to continue");
		refresh();
		getch();
		endwin();
		return 0;
	}

	nodelay(stdscr, true);


	init_pair(1, titlebar_fore, titlebar_back);
	init_pair(2, editor_fore, editor_back);
	init_pair(3, editor_back, editor_fore);
	init_pair(4, alert_fore, alert_back);
	init_pair(5, h_int, editor_back);
	init_pair(6, h_str, editor_back);
	init_pair(7, win_fore, win_back);
	init_pair(8, win_close, win_back);
	init_pair(9, COLOR_BLACK, COLOR_BLACK);
	init_pair(10, time_fore, time_back);

	showAlert("Welcome to YEDIT.");

	while (run) {
		attroff(A_ALTCHARSET);
		maxx = getmaxx(stdscr);
		maxy = getmaxy(stdscr);
		// render
		move(0, 0);
		attron(COLOR_PAIR(1));
		// titlebar
		for (uint16_t i = 0; i<maxx - 3; ++i) {
			addch(' ');
		}
		attroff(COLOR_PAIR(1));
		// render editor area
		move(1, 0);
		attron(COLOR_PAIR(2));
		for (uint16_t i = 0; i<maxx; ++i) {
			for (uint16_t j = 1; j<maxy; ++j) {
				addch(' ');
			}
		}
		move(2, 1);
		// render editor text
		lines = 0;
		cols  = 0;
		attroff(COLOR_PAIR(3));
		attron(COLOR_PAIR(2));
		renderCurs = false;
		inString = false;
		for (uint64_t i = 0; i<=fbuf.length(); ++i) {
			if (fbuf[i] == 10) {
				++ lines;
				cols = 0;
			}
			else
				++ cols;
			if ((lines >= scrollY) && (lines-scrollY < maxy)) {
				if ((i == curp) && (!renderCurs)) {
					curx = cols - 1;
					cury = lines;
					attroff(COLOR_PAIR(5));
					attroff(COLOR_PAIR(6));
					attron(COLOR_PAIR(3));
					if ((fbuf[i] == '"') || (fbuf[i] == '\''))
						inString = false;
					renderCurs = true;
				}
				else {
					attroff(COLOR_PAIR(3));
					attron(COLOR_PAIR(2));
					if (syntaxHighlighting) {
						if (((fbuf[i] == '"') || (fbuf[i] == '\'')) && (fbuf[i-1] != '\\')) {
							inString = !inString;
						}
						temp = "" + fbuf[i];
						if (!inString && ((fbuf[i] >= '0') && (fbuf[i] <= '9'))) {
							attron(COLOR_PAIR(5));
						}
						else if (inString) {
							attron(COLOR_PAIR(6));
						}
						else {
							attroff(COLOR_PAIR(5));
							attroff(COLOR_PAIR(6));
							attron(COLOR_PAIR(2));
						}
					}
				}
				if ((cols < maxx-1) && (i != fbuf.length())) {
					switch (fbuf[i]) {
						case 10: {
							if (i == curp) {
								attroff(COLOR_PAIR(5));
								attroff(COLOR_PAIR(6));
								attron(COLOR_PAIR(3));
								addch(' ');
								attroff(COLOR_PAIR(3));
								attron(COLOR_PAIR(2));
								renderCurs = true;
							}
							move((lines-scrollY)+2, 1);
							break;
						}
						case 9: {
							for (uint8_t i = 0; i<tabWidth; ++i) {
								addch(' ');
								attroff(COLOR_PAIR(3));
								attron(COLOR_PAIR(2));
							}
							cols += tabWidth-1;
							break;
						}
						default: {
							addch(fbuf[i]);
							break;
						}
					}
				}
				if ((cols < maxx-1) && (i == fbuf.length())) {
					addch(' ');
				}
			}
		}
		attroff(COLOR_PAIR(3));
		attron(COLOR_PAIR(2));
		rectangle(1, 0, maxy - 1, maxx - 1);
		// bottom bar
		temp = "(" +to_string(curx) +":" +to_string(cury) + ")";
		move(maxy-1, maxx-1-temp.length());
		printw(temp.c_str());
		// titlebar
		move(1, 1);
		printw("%s", fname.c_str());
		move(0, maxx-currentTime().length());
		attron(COLOR_PAIR(10));
		printw("%s", currentTime().c_str());
		attron(COLOR_PAIR(10));
		// render alerts if there is one
		if (alert) {
			move((maxy-1)/2, (maxx/2)-(alertContent.length()/2));
			attron(COLOR_PAIR(4));
			printw("[ %s ]", alertContent.c_str());
			alertDuration -= 1000/MAX_FPS;
			if (alertDuration <= 0)
				alert = false;
		}
		// titlebar
		move(0, 0);
		attroff(COLOR_PAIR(2));
		attron(COLOR_PAIR(1));
		printw(APP_NAME " | ");
		attroff(COLOR_PAIR(1));
		// render windows
		if (renderHelpMenu) {
			helpMenu.Move(maxx-22, maxy - helpMenu.h - 2);
			helpMenu.render();
		}
		if (dialogInFocus) {
			dialogMenu.render();
			dialog.render();
		}
		++ frames;
		refresh();
		usleep(1000000/MAX_FPS);
		// now input
		in = getch();

		if (!dialogInFocus) {
			switch (in) {
				case 10: {
					if (countLines(fbuf.substr(0, curp))-scrollY >= maxy-2)
						++ scrollY;
				}
				default: {
					if ((in == 10) || (in == 9) || (in >= 32 && in <= 126)) {
						fbuf.insert(curp, string(1, in));
						++ curp;
					};

					break;
				}
				case KEY_LEFT: {
					if (curp - 1 != -1) {
						-- curp;
					}
					break;
				}
				case KEY_RIGHT: {
					if (curp + 1 <= fbuf.length()) {
						++ curp;
					}
					break;
				}
				case KEY_UP: {
					if (scrollY-1 != -1)
						-- scrollY;
					break;
				}
				case KEY_DOWN: {
					if (scrollY+1 <= countLines(fbuf))
						++ scrollY;
					break;
				}
				case 127: case KEY_BACKSPACE: {
					if (curp-1 != -1) {
						fbuf.erase(curp-1, 1);
						-- curp;
					}
					break;
				}
			}
		} else {
			dialog.input(in);
		}
		switch (in) {
			case ctrl('s'): {
				if (fexists(fname)) {
					ofile.open(fname);
					ofile << fbuf.c_str();
					ofile.close();
					showAlert("Saved buffer to " +fname);
				}
				else
					showAlert("Error saving file");
				break;
			}
			case ctrl('q'): {
				run = false;
				break;
			}
			case ctrl('l'): {
				dialogInFocus = true;
				dialog.Move(maxx / 2 - 19, maxy / 2);
				dialogMenu.Move(maxx / 2 - 20, maxy / 2 - 2);
				break;
			};
			case 10: {
				if (dialogInFocus) {
					dialogInFocus = false;
					string lfname = dialog.content;
					if (fexists(lfname)) {
						fname = lfname;
						fbuf = fread(lfname);
						curp = 0;
						curx = 0;
						showAlert("Loaded file " +fname);
					}
					else {
						fname = lfname;
						fbuf = fread(lfname);
						curp = 0;
						curx = 0;
						showAlert("Created empty buffer " +fname);
					}
				};

				break;
			};
			case ctrl('g'): {
				syntaxHighlighting = !syntaxHighlighting;
				if (syntaxHighlighting)
					showAlert("Enabled syntax highlighting");
				else
					showAlert("Disabled syntax highlighting");
				break;
			}
			case ctrl('h'): {
				renderHelpMenu = !renderHelpMenu;
				break;
			}
			case ctrl('r'): {
				if (o_fexists((std::string) getenv("HOME") + "/.config/yedit/yedit.ini")) {
					bool err = false;
					try {
						settings.Parse(fread((std::string) getenv("HOME") +"/.config/yedit/yedit.ini"));
					} catch (const INI::ParserException& Error) {
						showAlert("Broken yedit.ini\n");
						err = true;
					}
					if (!settingsExist(settings)) {
						showAlert("Non-existant required properties in yedit.ini\n");
						err = true;
					}
					if (!err) {
						editor_back   = settings.AsInteger("appearence", "editor_b");
						editor_fore   = settings.AsInteger("appearence", "editor_f");
						titlebar_back = settings.AsInteger("appearence", "titlebar_b");
						titlebar_fore = settings.AsInteger("appearence", "titlebar_f");
						alert_back    = settings.AsInteger("appearence", "alert_b");
						alert_fore    = settings.AsInteger("appearence", "alert_f");
						tabWidth      = settings.AsInteger("editor", "tab-width");
						init_pair(1, titlebar_fore, titlebar_back);
						init_pair(2, editor_fore, editor_back);
						init_pair(3, editor_back, editor_fore);
						init_pair(4, alert_fore, alert_back);
						init_pair(5, h_int, editor_back);
						init_pair(6, h_str, editor_back);
						init_pair(7, win_fore, win_back);
						init_pair(8, win_close, win_back);
						init_pair(9, COLOR_BLACK, COLOR_BLACK);
						showAlert("Refreshed config");
					}
				}
				else {
					showAlert("yedit.ini not found");
				}
			}
		}
	}
	endwin();
	return 0;
}
