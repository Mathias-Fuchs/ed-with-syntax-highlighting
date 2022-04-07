#include "sh.h"
#include "srchilite/sourcehighlight.h"

#include <sstream>


// we highlight to the console, through ANSI escape sequences
static srchilite::SourceHighlight sourceHighlight("esc.outlang");
static std::stringstream ips, ops;

// assume out is preallocated to at least 1000 characters
void highlight(const char* input, int len, char* out, int* nchar) {
    ips.clear();
    ops.clear();
    for (int i = 0; i < len; i++) ips << input[i];
    sourceHighlight.highlight(ips, ops, "cpp.lang");
    int bytesWritten = 0;
    for (char c; ops.get(c) && bytesWritten < 999; ) 
        out[bytesWritten++] = c;

    // null-terminate the result
    out[bytesWritten] = '\0';

    // tell them proudly how many bytes we gave them
    *nchar = bytesWritten;
}

