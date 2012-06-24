#ifndef YMD_PRINT_H
#define YMD_PRINT_H

#define yEND     "%{a}"
#define yRED     "%{b}"
#define yGREEN   "%{c}"
#define yYELLOW  "%{d}"
#define yBLUE    "%{e}"
#define yPURPLE  "%{f}"
#define yAZURE   "%{g}"
#define yDRED    "%{h}"
#define yDGREEN  "%{i}"
#define yDYELLOW "%{j}"
#define yDBLUE   "%{k}"
#define yDPURPLE "%{l}"
#define yDAZURE  "%{m}"

int ymd_set_colored(int on);

// Example:
// ymd_print(yGREEN"[===]"yEND" %d", i);
int ymd_printf(const char *fmt, ...);

#endif // YMD_PRINT_H
