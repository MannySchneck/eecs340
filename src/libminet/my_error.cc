#include <cerrno>
#include <cstring>
#include <stdio.h>
#include "colors.h"
#include "my_error.h"

#define ERROR_LINE_START "\n%-13s"

void my_error_at_line(int err_code, int errno_given,  const char* func_name, const char* file_name, int line){
        if(errno_given){ 
                fprintf(stderr, ANSI_COLORIZE("%s", RED) "\terr_code %d  : %s",
                        "ERROR: ", err_code, strerror(err_code));
        }
        else{
                fprintf(stderr, ANSI_COLORIZE("%s", RED) "\tret_code %d ",
                        "ERROR: ", err_code);
        }
        fprintf(stderr, ERROR_LINE_START "%s%s", "\tIn file", ": ", file_name);
        fprintf(stderr, ERROR_LINE_START "%s%s", "\tIn function", ": ", func_name);
        fprintf(stderr, ERROR_LINE_START "%s%d\n", "\tAt line",": ", line);
}
