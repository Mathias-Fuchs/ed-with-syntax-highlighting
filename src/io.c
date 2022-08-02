/* io.c: i/o routines for the ed line editor */
/* GNU ed - The GNU line editor.
   Copyright (C) 1993, 1994 Andrew Moore, Talke Studio
   Copyright (C) 2006-2022 Antonio Diaz Diaz.
   Modification for syntax highlighting Copyright (C) 2022 Mathias Fuchs

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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ed.h"
#include "sh.h"

static const line_t * unterminated_line = 0;	/* last line has no '\n' */
static int linenum_ = 0;			/* script line number */
static const char* lang = "cpp.lang";  /* argument for source-highlight */

int linenum( void ) { return linenum_; }

void reset_unterminated_line( void ) { unterminated_line = 0; }

bool set_lang( const char* const s )
 {
 static char buf[516];
 const int len = strlen( s );
 memcpy( buf, s, len + 1 );
 lang = buf;
 return true;
 }

void unmark_unterminated_line( const line_t * const lp )
  { if( unterminated_line == lp ) unterminated_line = 0; }

static bool unterminated_last_line( void )
  { return ( unterminated_line != 0 &&
             unterminated_line == search_line_node( last_addr() ) ); }


/* print text to stdout */
static void print_line( const char * p, int len, const int pflags )
  {

  char out[1000];
  int nbytes;
  highlight(p, len, out, &nbytes, lang);
  p = out;
  len = nbytes;

  const char escapes[] = "\a\b\f\n\r\t\v";
  const char escchars[] = "abfnrtv";
  int col = 0;

  if( pflags & pf_n ) { printf( "%d\t", current_addr() ); col = 8; }
  while( --len >= 0 )
    {
    const unsigned char ch = *p++;
    if( !( pflags & pf_l ) ) putchar( ch );
    else
      {
      if( ++col > window_columns() ) { col = 1; fputs( "\\\n", stdout ); }
      if( ch >= 32 && ch <= 126 )
        { if( ch == '$' || ch == '\\' ) { ++col; putchar('\\'); }
          putchar( ch ); }
      else
        {
        char * const p = strchr( escapes, ch );
        ++col; putchar('\\');
        if( ch && p ) putchar( escchars[p-escapes] );
        else
          {
          col += 2;
          putchar( ( ( ch >> 6 ) & 7 ) + '0' );
          putchar( ( ( ch >> 3 ) & 7 ) + '0' );
          putchar( ( ch & 7 ) + '0' );
          }
        }
      }
    }
  if( !traditional() && ( pflags & pf_l ) ) putchar('$');
  putchar('\n');
  }


/* print a range of lines to stdout */
bool print_lines( int from, const int to, const int pflags )
  {
  line_t * const ep = search_line_node( inc_addr( to ) );
  line_t * bp = search_line_node( from );

  if( !from ) { invalid_address(); return false; }
  while( bp != ep )
    {
    const char * const s = get_sbuf_line( bp );
    if( !s ) return false;
    set_current_addr( from++ );
    print_line( s, bp->len, pflags );
    bp = bp->q_forw;
    }
  return true;
  }


/* return the parity of escapes at the end of a string */
static bool trailing_escape( const char * const s, int len )
  {
  bool odd_escape = false;
  while( --len >= 0 && s[len] == '\\' ) odd_escape = !odd_escape;
  return odd_escape;
  }


/* If *ibufpp contains an escaped newline, get an extended line (one
   with escaped newlines) from stdin.
   The backslashes escaping the newlines are stripped.
   Return line length in *lenp, including the trailing newline. */
bool get_extended_line( const char ** const ibufpp, int * const lenp,
                        const bool strip_escaped_newlines )
  {
  static char * buf = 0;
  static int bufsz = 0;
  int len;

  for( len = 0; (*ibufpp)[len++] != '\n'; ) ;
  if( len < 2 || !trailing_escape( *ibufpp, len - 1 ) )
    { if( lenp ) *lenp = len; return true; }
  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return false;
  memcpy( buf, *ibufpp, len );
  --len; buf[len-1] = '\n';			/* strip trailing esc */
  if( strip_escaped_newlines ) --len;		/* strip newline */
  while( true )
    {
    int len2;
    const char * const s = get_stdin_line( &len2 );
    if( !s ) return false;			/* error */
    if( len2 <= 0 ) return false;		/* EOF */
    if( !resize_buffer( &buf, &bufsz, len + len2 + 1 ) ) return false;
    memcpy( buf + len, s, len2 );
    len += len2;
    if( len2 < 2 || !trailing_escape( buf, len - 1 ) ) break;
    --len; buf[len-1] = '\n';			/* strip trailing esc */
    if( strip_escaped_newlines ) --len;		/* strip newline */
    }
  buf[len] = 0;
  *ibufpp = buf;
  if( lenp ) *lenp = len;
  return true;
  }


/* Read a line of text from stdin.
   Incomplete lines (lacking the trailing newline) are discarded.
   Return pointer to buffer and line size (including trailing newline),
   or 0 if error, or *sizep = 0 if EOF.
*/
const char * get_stdin_line( int * const sizep )
  {
  static char * buf = 0;
  static int bufsz = 0;
  int i = 0;

  while( true )
    {
    const int c = getchar();
    if( !resize_buffer( &buf, &bufsz, i + 2 ) ) { *sizep = 0; return 0; }
    if( c == EOF )
      {
      if( ferror( stdin ) )
        {
        show_strerror( "stdin", errno );
        set_error_msg( "Cannot read stdin" );
        clearerr( stdin );
        *sizep = 0; return 0;
        }
      if( feof( stdin ) )
        {
        set_error_msg( "Unexpected end-of-file" );
        clearerr( stdin );
        buf[0] = 0; *sizep = 0; if( i > 0 ) ++linenum_;	/* discard line */
        return buf;
        }
      }
    else
      {
      buf[i++] = c; if( !c ) set_binary(); if( c != '\n' ) continue;
      ++linenum_; buf[i] = 0; *sizep = i;
      return buf;
      }
    }
  }


/* Read a line of text from a stream.
   Return pointer to buffer and line size (including trailing newline
   if it exists and is not added now).
*/
static const char * read_stream_line( const char * const filename,
                                      FILE * const fp, int * const sizep,
                                      bool * const newline_addedp )
  {
  static char * buf = 0;
  static int bufsz = 0;
  int c, i = 0;

  while( true )
    {
    if( !resize_buffer( &buf, &bufsz, i + 2 ) ) return 0;
    c = getc( fp ); if( c == EOF ) break;
    buf[i++] = c;
    if( !c ) set_binary();
    else if( c == '\n' )		/* remove CR only from CR/LF pairs */
      { if( strip_cr() && i > 1 && buf[i-2] == '\r' ) { buf[i-2] = '\n'; --i; }
        break; }
    }
  buf[i] = 0;
  if( c == EOF )
    {
    if( ferror( fp ) )
      {
      show_strerror( filename, errno );
      set_error_msg( "Cannot read input file" );
      return 0;
      }
    else if( i )
      {
      buf[i] = '\n'; buf[i+1] = 0; *newline_addedp = true;
      if( !isbinary() ) ++i;
      }
    }
  *sizep = i;
  return buf;
  }


/* read a stream into the editor buffer;
   return total size of data read, or -1 if error */
static long read_stream( const char * const filename, FILE * const fp,
                         const int addr )
  {
  line_t * lp = search_line_node( addr );
  undo_t * up = 0;
  long total_size = 0;
  const bool o_isbinary = isbinary();
  const bool appended = ( addr == last_addr() );
  const bool o_unterminated_last_line = unterminated_last_line();
  bool newline_added = false;

  set_current_addr( addr );
  while( true )
    {
    int size = 0;
    const char * const s =
      read_stream_line( filename, fp, &size, &newline_added );
    if( !s ) return -1;
    if( size <= 0 ) break;
    total_size += size;
    disable_interrupts();
    if( !put_sbuf_line( s, size + newline_added ) )
      { enable_interrupts(); return -1; }
    lp = lp->q_forw;
    if( up ) up->tail = lp;
    else
      {
      up = push_undo_atom( UADD, current_addr(), current_addr() );
      if( !up ) { enable_interrupts(); return -1; }
      }
    enable_interrupts();
    }
  if( addr && appended && total_size && o_unterminated_last_line )
    fputs( "Newline inserted\n", stdout );		/* before stream */
  else if( newline_added && ( !appended || !isbinary() ) )
    fputs( "Newline appended\n", stdout );		/* after stream */
  if( !appended && isbinary() && !o_isbinary && newline_added )
    ++total_size;
  if( appended && isbinary() && ( newline_added || total_size == 0 ) )
    unterminated_line = search_line_node( last_addr() );
  return total_size;
  }


/* Read a named file/pipe into the buffer.
   Return line count, -1 if file not found, -2 if fatal error.
*/
int read_file( const char * const filename, const int addr )
  {
  FILE * fp;
  long size;
  int ret;

  if( *filename == '!' ) fp = popen( filename + 1, "r" );
  else
    {
    const char * const stripped_name = strip_escapes( filename );
    if( !stripped_name ) return -2;
    fp = fopen( stripped_name, "r" );
    }
  if( !fp )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot open input file" );
    return -1;
    }
  size = read_stream( filename, fp, addr );
  if( *filename == '!' ) ret = pclose( fp ); else ret = fclose( fp );
  if( size < 0 ) return -2;
  if( ret != 0 )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot close input file" );
    return -2;
    }
  if( !scripted() ) printf( "%lu\n", size );
  return current_addr() - addr;
  }


/* write a range of lines to a stream */
static long write_stream( const char * const filename, FILE * const fp,
                          int from, const int to )
  {
  line_t * lp = search_line_node( from );
  long size = 0;

  while( from && from <= to )
    {
    int len;
    char * p = get_sbuf_line( lp );
    if( !p ) return -1;
    len = lp->len;
    if( from != last_addr() || !isbinary() || !unterminated_last_line() )
      p[len++] = '\n';
    size += len;
    while( --len >= 0 )
      if( fputc( *p++, fp ) == EOF )
        {
        show_strerror( filename, errno );
        set_error_msg( "Cannot write file" );
        return -1;
        }
    ++from; lp = lp->q_forw;
    }
  return size;
  }


/* write a range of lines to a named file/pipe; return line count */
int write_file( const char * const filename, const char * const mode,
                const int from, const int to )
  {
  FILE * fp;
  long size;
  int ret;

  if( *filename == '!' ) fp = popen( filename + 1, "w" );
  else
    {
    const char * const stripped_name = strip_escapes( filename );
    if( !stripped_name ) return -1;
    fp = fopen( stripped_name, mode );
    }
  if( !fp )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot open output file" );
    return -1;
    }
  size = write_stream( filename, fp, from, to );
  if( *filename == '!' ) ret = pclose( fp ); else ret = fclose( fp );
  if( size < 0 ) return -1;
  if( ret != 0 )
    {
    show_strerror( filename, errno );
    set_error_msg( "Cannot close output file" );
    return -1;
    }
  if( !scripted() ) printf( "%lu\n", size );
  return ( from && from <= to ) ? to - from + 1 : 0;
  }
