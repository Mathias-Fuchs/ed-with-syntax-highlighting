/* GNU ed - The GNU line editor - sh.cpp
   Copyright (C) 2022 Mathias Fuchs
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "sh.h"
#include "srchilite/sourcehighlight.h"

#include <sstream>
#include <string.h>


// we highlight to the console, through ANSI escape sequences
static srchilite::SourceHighlight sourceHighlight("esc.outlang");
static std::stringstream ips, ops;

// assume out is preallocated to at least 1000 characters
void highlight(const char* input, int len, char* out, int* nchar, const char* lang) {
    ips.clear();
    ops.clear();
    for (int i = 0; i < len; i++) ips << input[i];
    sourceHighlight.highlight(ips, ops, lang);
    int bytesWritten = 0;
    for (char c; ops.get(c) && bytesWritten < 999; ) 
        out[bytesWritten++] = c;

    // null-terminate the result
    out[bytesWritten] = '\0';

    // tell them proudly how many bytes we gave them
    *nchar = bytesWritten;
}

