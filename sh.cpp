#include "sh.h"
#include "srchilite/sourcehighlight.h"
#include "srchilite/langmap.h"

#include <cstring>
#include <sstream>


// we highlight to the console, through ANSI escape sequences
static srchilite::SourceHighlight sourceHighlight("esc.outlang");



// assume out is preallocated to at least 1000 characters
void doit(const char* input, int len, char* out, int* nchar) {

    std::stringstream ips, ops;

    ips << input ;
    sourceHighlight.highlight(ips, ops, "cpp.lang");

    std::string outt = ops.str();

    // copy at most 999 characters over, one left for the null terminator
    size_t ol = outt.length();
    size_t bytesToCopy = ol < 999? ol : 999;
    
    std::memcpy(out, outt.c_str(), bytesToCopy);
    
    // null-terminate the result
    out[bytesToCopy] = '\0';

    // tell them proudly how many bytes we gave them
    *nchar = bytesToCopy;
}

