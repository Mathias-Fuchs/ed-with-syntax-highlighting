

// when compiling sh.cpp (which includes this file), this will give a C-style symbol in the object file
// when compiling a C file which includes this file, that symbol is made available to that C file
#ifdef __cplusplus
extern "C"
#endif

void highlight(const char* input, int len, char* out, int* nchar);
