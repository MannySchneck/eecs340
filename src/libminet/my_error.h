#ifndef MY_ERROR_H
#define MY_ERROR_H

#define ERROR_LINE_START "\n%-13s"

void my_error_at_line(int err_code, int errno_given,  const char* func_name, const char* file_name, int line);

#endif /* MY_ERROR_H */
