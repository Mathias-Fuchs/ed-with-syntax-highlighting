/* regex.c: regular expression interface routines for the ed line editor. */
/* GNU ed - The GNU line editor.
   Copyright (C) 1993, 1994 Andrew Moore, Talke Studio
   Copyright (C) 2006-2022 Antonio Diaz Diaz.

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

#include <stddef.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ed.h"


static const char * const inv_i_suf   = "Suffix 'I' not allowed on empty regexp";
static const char * const inv_pat_del = "Invalid pattern delimiter";
static const char * const mis_pat_del = "Missing pattern delimiter";
static const char * const no_match    = "No match";
static const char * const no_prev_pat = "No previous pattern";
static regex_t * last_regexp = 0;	/* pointer to last regex found */
static regex_t * subst_regexp = 0;	/* regex of last substitution */

static char * rbuf = 0;			/* replacement buffer */
static int rbufsz = 0;			/* replacement buffer size */
static int rlen = 0;			/* replacement length */


bool subst_regex( void ) { return subst_regexp != 0; }


/* translate characters in a string */
static void translit_text( char * p, int len, const char from, const char to )
  {
  while( --len >= 0 )
    {
    if( *p == from ) *p = to;
    ++p;
    }
  }


/* overwrite newlines with ASCII NULs */
static void newline_to_nul( char * const s, const int len )
  { translit_text( s, len, '\n', '\0' ); }

/* overwrite ASCII NULs with newlines */
static void nul_to_newline( char * const s, const int len )
  { translit_text( s, len, '\0', '\n' ); }


/* expand a POSIX character class */
static const char * parse_char_class( const char * p )
  {
  char c, d;

  if( *p == '^' ) ++p;
  if( *p == ']' ) ++p;
  for( ; *p != ']' && *p != '\n'; ++p )
    if( *p == '[' && ( ( d = p[1] ) == '.' || d == ':' || d == '=' ) )
      for( ++p, c = *++p; *p != ']' || c != d; ++p )
        if( ( c = *p ) == '\n' )
          return 0;
  return ( ( *p == ']' ) ? p : 0 );
  }


/* Copy a pattern string from the command buffer. If successful, return a
   pointer to the copy and point *ibufpp to the closing delimiter or final
   newline. */
static char * extract_pattern( const char ** const ibufpp, const char delimiter )
  {
  static char * buf = 0;
  static int bufsz = 0;
  const char * nd = *ibufpp;
  int len;

  while( *nd != delimiter && *nd != '\n' )
    {
    if( *nd == '[' )
      {
      nd = parse_char_class( ++nd );
      if( !nd ) { set_error_msg( "Unbalanced brackets ([])" ); return 0; }
      }
    else if( *nd == '\\' && *++nd == '\n' )
      { set_error_msg( "Trailing backslash (\\)" ); return 0; }
    ++nd;
    }
  len = nd - *ibufpp;
  if( !resize_buffer( &buf, &bufsz, len + 1 ) ) return 0;
  memcpy( buf, *ibufpp, len );
  buf[len] = 0;
  *ibufpp = nd;
  if( isbinary() ) nul_to_newline( buf, len );
  return buf;
  }


/* Return pointer to compiled regex (last_regexp), different from subst_regexp.
   Return 0 if error.
*/
static regex_t * compile_regex( const char * const pat, const bool ignore_case )
  {
  static regex_t store[3];		/* space for three compiled regexes */
  regex_t * exp;
  int n;

  for( n = 0; n < 3; ++n )
    if( ( exp = &store[n] ) != last_regexp && exp != subst_regexp ) break;
  const int cflags = ( extended_regexp() ? REG_EXTENDED : 0 ) |
                     ( ignore_case ? REG_ICASE : 0 );
  n = regcomp( exp, pat, cflags );
  if( n )
    {
    char buf[80];
    regerror( n, last_regexp, buf, sizeof buf );
    set_error_msg( buf );
    return 0;
    }
  /* free last_regexp if compiled and different from subst_regexp */
  if( last_regexp && last_regexp != subst_regexp ) regfree( last_regexp );
  last_regexp = exp;
  return last_regexp;
  }


/* return pointer to compiled regex from command buffer, or to previous
   compiled regex if empty RE. return 0 if error */
static regex_t * get_compiled_regex( const char ** const ibufpp )
  {
  const char delimiter = **ibufpp;

  if( delimiter == ' ' || delimiter == '\n' )
    { set_error_msg( inv_pat_del ); return 0; }
  if( *++*ibufpp == delimiter || **ibufpp == '\n' )	/* empty RE */
    {
    if( !last_regexp ) { set_error_msg( no_prev_pat ); return 0; }
    if( **ibufpp == delimiter && *++*ibufpp == 'I' )	/* remove delimiter */
      { set_error_msg( inv_i_suf ); return 0; }
    return last_regexp;
    }
  else
    {
    const char * const pat = extract_pattern( ibufpp, delimiter );
    if( !pat ) return 0;
    bool ignore_case = false;
    if( **ibufpp == delimiter && *++*ibufpp == 'I' )	/* remove delimiter */
      { ignore_case = true; ++*ibufpp; }		/* remove suffix */
    return compile_regex( pat, ignore_case );
    }
  }


/* Extract RE pattern (may be empty) from the command buffer.
   Return 0 if error.
*/
const char * get_pattern_for_s( const char ** const ibufpp )
  {
  const char delimiter = **ibufpp;

  if( delimiter == ' ' || delimiter == '\n' )
    { set_error_msg( inv_pat_del ); return 0; }
  if( *++*ibufpp == delimiter )				/* empty RE */
    {
    if( !last_regexp ) { set_error_msg( no_prev_pat ); return 0; }
    return "";
    }
  const char * const pat = extract_pattern( ibufpp, delimiter );
  if( !pat ) return 0;
  if( **ibufpp != delimiter ) { set_error_msg( mis_pat_del ); return 0; }
  return pat;
  }


bool set_subst_regex( const char * const pat, const bool ignore_case )
  {
  if( !pat ) return false;
  if( !*pat && ignore_case ) { set_error_msg( inv_i_suf ); return false; }

  disable_interrupts();
  regex_t * exp = *pat ? compile_regex( pat, ignore_case ) : last_regexp;
  if( exp && exp != subst_regexp )
    {
    if( subst_regexp ) regfree( subst_regexp );
    subst_regexp = exp;
    }
  enable_interrupts();
  return ( exp ? true : false );
  }


/* set subst_regexp to last RE found */
bool replace_subst_re_by_search_re( void )
  {
  if( !last_regexp ) { set_error_msg( no_prev_pat ); return false; }
  if( last_regexp != subst_regexp )
    {
    disable_interrupts();
    if( subst_regexp ) regfree( subst_regexp );
    subst_regexp = last_regexp;
    enable_interrupts();
    }
  return true;
  }


/* add lines matching a regular expression to the global-active list */
bool build_active_list( const char ** const ibufpp, const int first_addr,
                        const int second_addr, const bool match )
  {
  int addr;

  const regex_t * const exp = get_compiled_regex( ibufpp );
  if( !exp ) return false;
  clear_active_list();
  const line_t * lp = search_line_node( first_addr );
  for( addr = first_addr; addr <= second_addr; ++addr, lp = lp->q_forw )
    {
    char * const s = get_sbuf_line( lp );
    if( !s ) return false;
    if( isbinary() ) nul_to_newline( s, lp->len );
    if( match == !regexec( exp, s, 0, 0, 0 ) && !set_active_node( lp ) )
      return false;
    }
  return true;
  }


/* return the address of the next line matching a regular expression in a
   given direction. wrap around begin/end of editor buffer if necessary */
int next_matching_node_addr( const char ** const ibufpp )
  {
  const bool forward = ( **ibufpp == '/' );
  const regex_t * const exp = get_compiled_regex( ibufpp );
  int addr = current_addr();

  if( !exp ) return -1;
  do {
    addr = ( forward ? inc_addr( addr ) : dec_addr( addr ) );
    if( addr )
      {
      const line_t * const lp = search_line_node( addr );
      char * const s = get_sbuf_line( lp );
      if( !s ) return -1;
      if( isbinary() ) nul_to_newline( s, lp->len );
      if( !regexec( exp, s, 0, 0, 0 ) ) return addr;
      }
    }
  while( addr != current_addr() );
  set_error_msg( no_match );
  return -1;
  }


/* Extract substitution replacement from the command buffer.
   If isglobal, newlines in command-list are unescaped. */
bool extract_replacement( const char ** const ibufpp, const bool isglobal )
  {
  static char * buf = 0;		/* temporary buffer */
  static int bufsz = 0;
  int i = 0;
  const char delimiter = **ibufpp;

  if( delimiter == '\n' ) { set_error_msg( mis_pat_del ); return false; }
  ++*ibufpp;
  if( **ibufpp == '%' &&		/* replacement is a single '%' */
      ( (*ibufpp)[1] == delimiter ||
        ( (*ibufpp)[1] == '\n' && ( !isglobal || (*ibufpp)[2] == 0 ) ) ) )
    {
    ++*ibufpp;
    if( !rbuf ) { set_error_msg( no_prev_subst ); return false; }
    return true;
    }
  while( **ibufpp != delimiter )
    {
    if( **ibufpp == '\n' && ( !isglobal || (*ibufpp)[1] == 0 ) ) break;
    if( !resize_buffer( &buf, &bufsz, i + 2 ) ) return false;
    if( ( buf[i++] = *(*ibufpp)++ ) == '\\' &&
        ( buf[i++] = *(*ibufpp)++ ) == '\n' && !isglobal )
      {
      /* not reached if isglobal; in command-list, newlines are unescaped */
      int size = 0;
      *ibufpp = get_stdin_line( &size );
      if( !*ibufpp ) return false;			/* error */
      if( size <= 0 ) return false;			/* EOF */
      }
    }
  /* make sure that buf gets allocated if empty replacement */
  if( !resize_buffer( &buf, &bufsz, i + 1 ) ) return false;
  buf[i] = 0;
  disable_interrupts();
  { char * p = buf; buf = rbuf; rbuf = p;		/* swap buffers */
    rlen = i; i = bufsz; bufsz = rbufsz; rbufsz = i; }
  enable_interrupts();
  return true;
  }


/* Produce replacement text from matched text and replacement template.
   Return new offset to end of replacement text, or -1 if error. */
static int replace_matched_text( char ** txtbufp, int * const txtbufszp,
                                 const char * const txt,
                                 const regmatch_t * const rm, int offset,
                                 const int re_nsub )
  {
  int i;

  for( i = 0 ; i < rlen; ++i )
    {
    int n;
    if( rbuf[i] == '&' )
      {
      int j = rm[0].rm_so; int k = rm[0].rm_eo;
      if( !resize_buffer( txtbufp, txtbufszp, offset - j + k ) ) return -1;
      while( j < k ) (*txtbufp)[offset++] = txt[j++];
      }
    else if( rbuf[i] == '\\' && rbuf[++i] >= '1' && rbuf[i] <= '9' &&
             ( n = rbuf[i] - '0' ) <= re_nsub )
      {
      int j = rm[n].rm_so; int k = rm[n].rm_eo;
      if( !resize_buffer( txtbufp, txtbufszp, offset - j + k ) ) return -1;
      while( j < k ) (*txtbufp)[offset++] = txt[j++];
      }
    else		/* preceding 'if' skipped escaping backslashes */
      {
      if( !resize_buffer( txtbufp, txtbufszp, offset + 1 ) ) return -1;
      (*txtbufp)[offset++] = rbuf[i];
      }
    }
  if( !resize_buffer( txtbufp, txtbufszp, offset + 1 ) ) return -1;
  (*txtbufp)[offset] = 0;
  return offset;
  }


/* Produce new text with one or all matches replaced in a line.
   Return size of the new line text, 0 if no change, -1 if error */
static int line_replace( char ** txtbufp, int * const txtbufszp,
                         const line_t * const lp, const int snum )
  {
  enum { se_max = 30 };	/* max subexpressions in a regular expression */
  regmatch_t rm[se_max];
  char * txt = get_sbuf_line( lp );
  const char * eot;
  int i = 0, offset = 0;
  const bool global = ( snum <= 0 );
  bool changed = false;

  if( !txt ) return -1;
  if( isbinary() ) nul_to_newline( txt, lp->len );
  eot = txt + lp->len;
  if( !regexec( subst_regexp, txt, se_max, rm, 0 ) )
    {
    int matchno = 0;
    bool infloop = false;
    do {
      if( global || snum == ++matchno )
        {
        changed = true; i = rm[0].rm_so;
        if( !resize_buffer( txtbufp, txtbufszp, offset + i ) ) return -1;
        if( isbinary() ) newline_to_nul( txt, rm[0].rm_eo );
        memcpy( *txtbufp + offset, txt, i ); offset += i;
        offset = replace_matched_text( txtbufp, txtbufszp, txt, rm, offset,
                                       subst_regexp->re_nsub );
        if( offset < 0 ) return -1;
        }
      else
        {
        i = rm[0].rm_eo;
        if( !resize_buffer( txtbufp, txtbufszp, offset + i ) ) return -1;
        if( isbinary() ) newline_to_nul( txt, i );
        memcpy( *txtbufp + offset, txt, i ); offset += i;
        }
      txt += rm[0].rm_eo;
      if( global && rm[0].rm_eo == 0 )
        { if( !infloop ) infloop = true;	/* 's/^/#/g' is valid */
          else { set_error_msg( "Infinite substitution loop" ); return -1; } }
      }
    while( *txt && ( !changed || global ) &&
           !regexec( subst_regexp, txt, se_max, rm, REG_NOTBOL ) );
    i = eot - txt;
    if( !resize_buffer( txtbufp, txtbufszp, offset + i + 2 ) ) return -1;
    if( isbinary() ) newline_to_nul( txt, i );
    memcpy( *txtbufp + offset, txt, i );		/* tail copy */
    memcpy( *txtbufp + offset + i, "\n", 2 );
    }
  return ( changed ? offset + i + 1 : 0 );
  }


/* for each line in a range, change text matching a regular expression
   according to a substitution template (replacement); return false if error */
bool search_and_replace( const int first_addr, const int second_addr,
                         const int snum, const bool isglobal )
  {
  static char * txtbuf = 0;		/* new text of line buffer */
  static int txtbufsz = 0;		/* new text of line buffer size */
  int addr = first_addr;
  int lc;
  bool match_found = false;

  for( lc = 0; lc <= second_addr - first_addr; ++lc, ++addr )
    {
    const line_t * const lp = search_line_node( addr );
    const int size = line_replace( &txtbuf, &txtbufsz, lp, snum );
    if( size < 0 ) return false;
    if( size )
      {
      const char * txt = txtbuf;
      const char * const eot = txtbuf + size;
      undo_t * up = 0;
      disable_interrupts();
      if( !delete_lines( addr, addr, isglobal ) )
        { enable_interrupts(); return false; }
      set_current_addr( addr - 1 );
      do {
        txt = put_sbuf_line( txt, eot - txt );
        if( !txt ) { enable_interrupts(); return false; }
        if( up ) up->tail = search_line_node( current_addr() );
        else
          {
          up = push_undo_atom( UADD, current_addr(), current_addr() );
          if( !up ) { enable_interrupts(); return false; }
          }
        }
      while( txt != eot );
      enable_interrupts();
      addr = current_addr();
      match_found = true;
      }
    }
  if( !match_found && !isglobal )
    { set_error_msg( no_match ); return false; }
  return true;
  }
