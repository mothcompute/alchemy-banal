#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <xmp.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include <unistd.h>
#include <errno.h>

// defines void* module and long modsize
#include "music.h"


char* hdr = "ALCHEMY [press A for info]\n-----------------+ CURRENT\n";

/* RUNE BITFIELD
 *
 * RRRRFFFB (very conveniently only 1 byte)
 *
 * R - rune type. theres probably not 16 but eh
 * F - foreground color. there are at least 5
 * B - theres only 2 so only 1 bit is needed
 *
 */

char* runes = " &*oImu%/avjyt^@>";

#define getbg(B) ((B) & 1)
#define getfg(B) (((B) >> 1) & 7)
#define getrune(B) ((B) >> 4)
uint8_t newrune() {
	uint8_t r = ((rand() & 0xFF) | 1);
	if(!getrune(r) || !getfg(r)) r = newrune();
	if(getrune(r) == 2 || getrune(r) == 1) r = (r & 0xF0) | 0xF;
	else if(getfg(r) == 7) r = newrune();
	return r;
}

uint8_t board[72];

struct winsize win;
struct termios term, term_b;

void clrscr() {
	// VERY good practice. will never cause bugs
	printf("\x1B\x5B\x48\x1B\x5B\x4A");
}

int closef = 0;
void winch(int sig) {
	ioctl(1, TIOCGWINSZ, &win);
	if(win.ws_col < 27 || win.ws_row < 10) {
		tcsetattr(0, TCSANOW, &term_b);
		printf("window too small, exiting\n");
		closef = 1;
	}
}

void posupdate(int x, int y) {
        printf("\033[%d;%dH", ++y, ++x);
}

void render_board(uint8_t* brd, int score, int level, int rune, int stage, int valid) {
	uint8_t* m = brd;
	for(int i = 0; i < 8; i++) {
		for(int c = 0; c < 9; c++) {
			printf(
				"\033[0;9%im", getfg(m[c]));

				printf("\033[0;%i%im", getbg(m[c]) ? 9 : 10, getbg(m[c]) ? getfg(m[c]) : 7);
			
			printf(
				"%c%s", runes[getrune(m[c])],
				(c == 8) ? "\033[0m|" : " "
			);
			if(c == 8) {
				//if(i == 0) printf(" :%c    %c", valid + '(', runes[rune]);
				if(i == 0) printf(" :%c    \033[0;9%im%c\033[0m", valid + '(', getfg(rune), runes[getrune(rune)]);
				if(i == 2) printf("   SCORE");
				if(i == 3) printf(" %07i", score);
				if(i == 5) {
					printf("   [");
					for(int v = 0; v < level; v++) putchar('#');
					for(int v = 0; v < 3 - level; v++) putchar('-');
					putchar(']');
				}
				if(i == 6) printf("   STAGE");
				if(i == 7) printf("      %02i", stage);
				putchar('\n');
			}
		}
		m += 9;
	}
}

int linecheck(uint8_t* board, int x, int y) {
	int w = 1, h = 1;
	for(int i = 0; i < 9; i++) if(!getrune(board[(y*9) + i])) w = 0;
	for(int i = 0; i < 8; i++) if(!getrune(board[(i*9) + x])) h = 0;

	if(w) for(int i = 0; i < 9; i++) board[(y*9) + i] = 0x0F;
	if(h) for(int i = 0; i < 8; i++) board[(i*9) + x] = 0x0F;

	return w | h;
}

int movecheck(uint8_t* board, int x, int y, uint8_t rune) {
	if(getrune(rune) == 1) if(getrune(board[(y*9) + x])) return 1;
	if(getrune(board[(y*9) + x])) return 0;
	int	l = x ? board[(y*9) + (x - 1)] : 0x0F,
		r = (x < 8) ? board[(y*9) + (x + 1)] : 0x0F,
		u = y ? board[((y-1)*9) + x] : 0x0F,
		d = (y < 7) ? board[((y+1)*9) + x] : 0x0F;
	if(getrune(rune) == 2) return !!getrune(l|r|u|d);

	int rnc = getfg(rune);
	int rnt = getrune(rune);

	int	lc = getfg(l) == rnc,
		rc = getfg(r) == rnc,
		uc = getfg(u) == rnc,
		dc = getfg(d) == rnc;
	int	lt = getrune(l) == rnt,
		rt = getrune(r) == rnt,
		ut = getrune(u) == rnt,
		dt = getrune(d) == rnt;

	if(l == 0x2F) lt++;
	if(r == 0x2F) rt++;
	if(u == 0x2F) ut++;
	if(d == 0x2F) dt++;

	int ac = 0;
	if(!getrune(l)) { lt++; ac++; }
	if(!getrune(r)) { rt++; ac++; }
	if(!getrune(u)) { ut++; ac++; }
	if(!getrune(d)) { dt++; ac++; }
	if(ac == 4) return 0;

	int ms = 4;
	if(lc || lt) ms--;
	if(rc || rt) ms--;
	if(uc || ut) ms--;
	if(dc || dt) ms--;

	return !ms;
}

int musplay = 1;

void fsig(int sig) {
	if(sig == SIGUSR1) musplay ^= 1;
	else closef = 1;
}

int main(int argc, char** argv) {

	// we can just call the signal
	// that is because c is the best
	// make sure window is big enough
	winch(0);

	pa_simple *s;
	pa_sample_spec ss;
	ss.format = PA_SAMPLE_S16NE;
	ss.channels = 2;
	ss.rate = 44100;

	int err;
	
	long id = getpid();
	long p = fork();
	if(!p) {
		uint8_t aubuf[44100];
		xmp_context xc = xmp_create_context();
		xmp_load_module_from_memory(xc, module, modsize);
		xmp_start_player(xc, 44100, 0);
		s = pa_simple_new(NULL, "alchemy-banal", PA_STREAM_PLAYBACK, NULL, "the game on earth", &ss, NULL, NULL, NULL);
		signal(SIGINT, fsig);
		signal(SIGUSR1, fsig);
		while(1) {
			if(closef || kill(id, 0) == ESRCH) {
				pa_simple_free(s);
				xmp_release_module(xc);
				xmp_free_context(xc);
				return 0;
			} else if(musplay) {
				xmp_play_buffer(xc, aubuf, 44100, 0);
				pa_simple_write(s, aubuf, 44100, &err);
			}
		}
	}

	// init boardmem
	memset(board, 0xE, 72);
	board[31] = 0x2F;

	// rng for runes
	srand(time(NULL));

	// trap window change. other
	// signals not necessary since
	// setting term attributes lets
	// us trap things like ^C
	signal(SIGWINCH, winch);

	// terminal config (capture all input)
	tcgetattr(0, &term);
	term_b = term;
	term.c_iflag &= ~(ICRNL|IXON);
	term.c_lflag &= ~(ICANON|ECHO|ISIG|IEXTEN);
	tcsetattr(0, TCSANOW, &term);
	setbuf(stdout, NULL);
	fcntl(0, F_SETFL, O_NONBLOCK);

	// game init complete
	// begin actual game loop
	clrscr();
	unsigned char	// peak formatting
			c,	// current input
			y = 0,	// current cursor x
			x = 0,	// current cursor y
			v = 1,	// move validity
		stage = 1,	// stage
		level = 0,	// current goop(?) level
		r = newrune(),	// current rune
		mode = 0;	// current mode

	unsigned int score = 0, aframe = 0, vr = 0;

	printf(hdr);
	render_board(board, 20, 0, r, 1, 1);
	goto eloop;
	while(1) {
		if(closef) goto cleanup;
		if((c = getchar()) != 255) {
		eloop:
			if(mode != 1) switch(c) {
				case ' ':
					// place rune
					if(!getrune(board[(y*9) + x]) && (getrune(r) > 2)) {
						// TODO movecheck
						if(movecheck(board, x, y, r)) board[(y*9) + x] = r;
						else goto nrr;
					} else if((getrune(r) == 2) && !getrune(board[(y*9) + x])) {
						board[(y*9) + x] = r;
					} else if((getrune(r) == 1) && getrune(board[(y*9) + x])) {
						board[(y*9) + x] = 0xF;
					}
					else goto nrr;
				rr:
					vr = 1;
					for(int z = 0; z < 72; z++) if(!getbg(board[z])) vr = 0;
					if(vr) {
						stage++;
						memset(board, 0xE, 72);
						board[31 + (((rand() & 1) ? -1 : 1) * (rand() & 0x7))] = 0x2F;
					}
					
					r = newrune();
					score++;
					if(level) level--;
					clrscr();
					printf(hdr);
					if(linecheck(board, x, y)) score += 4;
					render_board(board, score, level, r, stage, v);
					nrr: break;
				case 'x':
					// discard rune
					level++;
					r = newrune();
					clrscr();
					printf(hdr);
					render_board(board, score, level, r, stage, v);
					if(level >> 2) {
						clrscr();
						printf("you lost :/\n");
						printf(hdr);
						render_board(board, score, level, r, stage, v);
						goto cleanup;
					}
					break;
				case 'i':
					// up
					if(y != 0) y--;
					break;
				case 'j':
					// left
					if(x != 0) x--;
					break;
				case 'k':
					// down
					if(y < 7) y++;
					break;
				case 'l':
					// right
					if(x < 8) x++;
					break;
				case 'q':
					// quit
					clrscr();
					printf(hdr);
					render_board(board, score, level, r, stage, v);
					goto cleanup;
					break;
			}
			if(c == 'a') {
				mode ^= 1;
				kill(p, SIGUSR1);
				clrscr();
				if(mode == 1) {
					printf(
						"ALCHEMY BANAL\n"
						"original game by\n"
						"              PopCap Games\n"
						"this version by\n"
						"              @mothcompute\n"
						"CONTROLS:\n"
						"SPACE to place rune\n"
						"    X to discard rune\n"
						"    Q to quit\n"
						" IJKL to move cursor\n"
						"& = skull | * = wild"
					);
				} else {
				initbrd:
					printf(hdr);
					render_board(board, score, level, r, stage, movecheck(board, x, y, r));
				}
			}
			if(!mode) {
				clrscr();
				printf(hdr);
				render_board(board, score, level, r, stage, movecheck(board, x, y, r));
			}
		}
		if(!mode) posupdate(x*2, y + 2);
	}

cleanup:
	tcsetattr(0, TCSANOW, &term_b);
	kill(p, SIGINT);
}
