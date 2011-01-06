/*
Copyright (c) 2008-2010
	Lars-Dominik Braun <lars@6xq.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "ui_readline.h"

static inline void BarReadlineMoveLeft (char *buf, size_t *bufPos,
		size_t *bufLen) {
	char *tmpBuf = &buf[*bufPos-1];
	while (tmpBuf < &buf[*bufLen]) {
		*tmpBuf = *(tmpBuf+1);
		++tmpBuf;
	}
	--(*bufPos);
	--(*bufLen);
}

static inline char BarReadlineIsAscii (char b) {
	return !(b & (1 << 7));
}

static inline char BarReadlineIsUtf8Start (char b) {
	return (b & (1 << 7)) && (b & (1 << 6));
}

static inline char BarReadlineIsUtf8Content (char b) {
	return (b & (1 << 7)) && !(b & (1 << 6));
}

/*	readline replacement
 *	@param buffer
 *	@param buffer size
 *	@param accept these characters
 *	@return number of bytes read from stdin
 */
size_t BarReadline (char *buf, const size_t bufSize, const char *mask,
		BarReadlineFds_t *input, const BarReadlineFlags_t flags) {
	size_t bufPos = 0;
	size_t bufLen = 0;
	unsigned char escapeState = 0;
	fd_set set;

	assert (buf != NULL);
	assert (bufSize > 0);
	assert (input != NULL);

	memset (buf, 0, bufSize);

	/* if fd is a fifo fgetc will always return EOF if nobody writes to
	 * it, stdin will block */
	while (1) {
		int curFd = -1;
		char chr;

		/* select modifies set */
		memcpy (&set, &input->set, sizeof (set));
		if (select (input->maxfd, &set, NULL, NULL, NULL) < 0) {
			/* fail */
			break;
		}

		assert (sizeof (input->fds) / sizeof (*input->fds) == 2);
		if (FD_ISSET(input->fds[0], &set)) {
			curFd = input->fds[0];
		} else if (input->fds[1] != -1 && FD_ISSET(input->fds[1], &set)) {
			curFd = input->fds[1];
		}
		/* only check for stdin, fifo is "reopened" as soon as another writer
		 * is available */
		if (read (curFd, &chr, sizeof (chr)) <= 0 && curFd == STDIN_FILENO) {
			/* select() is going wild if fdset contains EOFed fd's */
			FD_CLR (curFd, &input->set);
			continue;
		}
		switch (chr) {
			/* EOT */
			case 4:
				fputs ("\n", stdout);
				return bufLen;
				break;

			/* return */
			case 10:
				fputs ("\n", stdout);
				return bufLen;
				break;

			/* escape */
			case 27:
				escapeState = 1;
				break;

			/* del */
			case 126:
				break;

			/* backspace */
			case 8: /* ASCII BS */
			case 127: /* ASCII DEL */
				if (bufPos > 0) {
					if (BarReadlineIsAscii (buf[bufPos-1])) {
						BarReadlineMoveLeft (buf, &bufPos, &bufLen);
					} else {
						/* delete utf-8 multibyte chars */
						/* char content */
						while (BarReadlineIsUtf8Content (buf[bufPos-1])) {
							BarReadlineMoveLeft (buf, &bufPos, &bufLen);
						}
						/* char length */
						if (BarReadlineIsUtf8Start (buf[bufPos-1])) {
							BarReadlineMoveLeft (buf, &bufPos, &bufLen);
						}
					}
					/* move caret back and delete last character */
					if (!(flags & BAR_RL_NOECHO)) {
						fputs ("\033[D\033[K", stdout);
						fflush (stdout);
					}
				} else if (bufPos == 0 && buf[bufPos] != '\0') {
					/* delete char at position 0 but don't move cursor any further */
					buf[bufPos] = '\0';
					if (!(flags & BAR_RL_NOECHO)) {
						fputs ("\033[K", stdout);
						fflush (stdout);
					}
				}
				break;

			default:
				/* ignore control/escape characters */
				if (chr <= 0x1F) {
					break;
				}
				if (escapeState == 2) {
					escapeState = 0;
					break;
				}
				if (escapeState == 1 && chr == '[') {
					escapeState = 2;
					break;
				}
				/* don't accept chars not in mask */
				if (mask != NULL && !strchr (mask, chr)) {
					break;
				}
				/* don't write beyond buffer's limits */
				if (bufPos < bufSize-1) {
					buf[bufPos] = chr;
					++bufPos;
					++bufLen;
					if (!(flags & BAR_RL_NOECHO)) {
						putchar (chr);
						fflush (stdout);
					}
					/* buffer full => return if requested */
					if ((flags & BAR_RL_FULLRETURN) && bufPos >= bufSize-1) {
						fputs ("\n", stdout);
						return bufLen;
					}
				}
				break;
		}
	}
	return 0;
}

/*	Read string from stdin
 *	@param buffer
 *	@param buffer size
 *	@return number of bytes read from stdin
 */
size_t BarReadlineStr (char *buf, const size_t bufSize,
		BarReadlineFds_t *input, const BarReadlineFlags_t flags) {
	return BarReadline (buf, bufSize, NULL, input, flags);
}

/*	Read int from stdin
 *	@param write result into this variable
 *	@return number of bytes read from stdin
 */
size_t BarReadlineInt (int *ret, BarReadlineFds_t *input) {
	int rlRet = 0;
	char buf[16];

	rlRet = BarReadline (buf, sizeof (buf), "0123456789", input,
			BAR_RL_DEFAULT);
	*ret = atoi ((char *) buf);

	return rlRet;
}

/*	Yes/No?
 *	@param default (user presses enter)
 */
bool BarReadlineYesNo (bool def, BarReadlineFds_t *input) {
	char buf[2];
	BarReadline (buf, sizeof (buf), "yYnN", input, BAR_RL_DEFAULT);
	if (*buf == 'y' || *buf == 'Y' || (def == true && *buf == '\0')) {
		return true;
	} else {
		return false;
	}
}

