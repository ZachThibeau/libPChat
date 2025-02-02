/************************************************************************
 *    This technique was borrowed in part from the source code to
 *    ircd-hybrid-5.3 to implement case-insensitive string matches which
 *    are fully compliant with Section 2.2 of RFC 1459, the copyright
 *    of that code being (C) 1990 Jarkko Oikarinen and under the GPL.
 *
 *    A special thanks goes to Mr. Okarinen for being the one person who
 *    seems to have ever noticed this section in the original RFC and
 *    written code for it.  Shame on all the rest of you (myself included).
 *
 *        --+ Dagmar d'Surreal
 */

#ifndef XCHAT_UTIL_H
#define XCHAT_UTIL_H

// used with StripColor
#define STRIP_COLOR 1
#define STRIP_ATTRIB 2
#define STRIP_HIDDEN 4
#define STRIP_ESCMARKUP 8
#define STRIP_ALL 7

#define rfc_tolower(c) (rfc_tolowertab[(unsigned char)(c)])

extern const unsigned char rfc_tolowertab[];

int my_poptParseArgvString(const char* s, int* argcPtr, char*** argvPtr);
char *expand_homedir(char *file);
void path_part(char *file, char *path, int pathlen);
int match(const char *mask, const char *string);
char *file_part(char *file);
void for_files(char *dirname, char *mask, void callback (char *file));
int rfc_casecmp(const char*, const char*);
int rfc_ncasecmp(char*, char*, int);
int buf_get_line(char*, char**, int*, int len);
char *nocasestrstr(const char *text, const char *tofind);
char *country(char *);
void country_search(char *pattern, void *ud, void (*print)(void*, char*, ...));
char *get_cpu_str();
int util_exec(const char *cmd);
int util_execv(char* const argv[]);
char *strip_color(const char *text, int len, int flags);
int strip_color2(const char *src, int len, char *dst, int flags);
int strip_hidden_attribute(char *src, char *dst);
char *errorstring(int err);
int waitline(int sok, char *buf, int bufsize, int);
#ifdef WIN32
int waitline2(GIOChannel *source, char *buf, int bufsize);
#else
#define waitline2(source,buf,size) waitline(serv->childread,buf,size,0)
#endif
unsigned long make_ping_time();
void move_file_utf8(char *src_dir, char *dst_dir, char *fname, int dccpermissions);
int mkdir_utf8(char *dir);
int token_foreach(char *str, char sep, int (*callback)(char *str, void *ud), void *ud);
unsigned int str_hash(const char *key);
unsigned int str_ihash(const unsigned char *key);
void safe_strcpy(char *dest, const char *src, int bytes_left);

#endif
