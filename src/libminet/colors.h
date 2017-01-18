#ifndef __COLORS_H__
#define __COLORS_H__


#define ANSI_RESET   "\x1b[0m"

#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define CYAN    "\x1b[36m"

#define ANSI_COLORIZE(str, color) color str ANSI_RESET

#endif /* __COLORS_H__ */

