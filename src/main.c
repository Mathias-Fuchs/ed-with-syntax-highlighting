/* GNU ed - The GNU line editor.
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
/*
   Exit status: 0 for a normal exit, 1 for environmental problems
   (file not found, invalid flags, I/O errors, etc), 2 to indicate a
   corrupt or invalid input file, 3 for an internal consistency error
   (e.g., bug) which caused ed to panic.
*/
/*
 * CREDITS
 *
 *      This program is based on the editor algorithm described in
 *      Brian W. Kernighan and P. J. Plauger's book "Software Tools
 *      in Pascal", Addison-Wesley, 1981.
 *
 *      The buffering algorithm is attributed to Rodney Ruddock of
 *      the University of Guelph, Guelph, Ontario.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <locale.h>

#include "carg_parser.h"
#include "ed.h"

static const char * const program_name = "ed";
static const char * const program_year = "2022";
static const char * invocation_name = "ed";		/* default value */

static bool extended_regexp_ = false;	/* if set, use EREs */
static bool restricted_ = false;	/* if set, run in restricted mode */
static bool scripted_ = false;		/* if set, suppress diagnostics,
					   byte counts and '!' prompt */
static bool strip_cr_ = false;		/* if set, strip trailing CRs */
static bool traditional_ = false;	/* if set, be backwards compatible */

/* Access functions for command line flags. */
bool extended_regexp( void ) { return extended_regexp_; }
bool restricted( void ) { return restricted_; }
bool scripted( void ) { return scripted_; }
bool strip_cr( void ) { return strip_cr_; }
bool traditional( void ) { return traditional_; }


static void show_help( void )
  {
  printf( "GNU ed is a line-oriented text editor. It is used to create, display,\n"
          "modify and otherwise manipulate text files, both interactively and via\n"
          "shell scripts. A restricted version of ed, red, can only edit files in\n"
          "the current directory and cannot execute shell commands. Ed is the\n"
          "'standard' text editor in the sense that it is the original editor for\n"
          "Unix, and thus widely available. For most purposes, however, it is\n"
          "superseded by full-screen editors such as GNU Emacs or GNU Moe.\n"
          "\nUsage: %s [options] [file]\n", invocation_name );
  printf( "\nOptions:\n"
          "  -h, --help                 display this help and exit\n"
	  "  -H, --highlight            set language for source-highlight\n"
          "  -V, --version              output version information and exit\n"
          "  -E, --extended-regexp      use extended regular expressions\n"
          "  -G, --traditional          run in compatibility mode\n"
          "  -l, --loose-exit-status    exit with 0 status even if a command fails\n"
          "  -p, --prompt=STRING        use STRING as an interactive prompt\n"
          "  -r, --restricted           run in restricted mode\n"
          "  -s, --quiet, --silent      suppress diagnostics, byte counts and '!' prompt\n"
          "  -v, --verbose              be verbose; equivalent to the 'H' command\n"
          "      --strip-trailing-cr    strip carriage returns at end of text lines\n"
          "\nStart edit by reading in 'file' if given.\n"
          "If 'file' begins with a '!', read output of shell command.\n"
          "\nExit status: 0 for a normal exit, 1 for environmental problems (file\n"
          "not found, invalid flags, I/O errors, etc), 2 to indicate a corrupt or\n"
          "invalid input file, 3 for an internal consistency error (e.g., bug) which\n"
          "caused ed to panic.\n"
          "\nReport bugs to bug-ed@gnu.org\n"
          "Ed home page: http://www.gnu.org/software/ed/ed.html\n"
          "General help using GNU software: http://www.gnu.org/gethelp\n" );
  }


static void show_version( void )
  {
  printf( "Copyright (C) 1994 Andrew L. Moore.\n"
          "Copyright (C) %s Antonio Diaz Diaz.\n", program_year );
  printf( "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
          "This is free software: you are free to change and redistribute it.\n"
          "There is NO WARRANTY, to the extent permitted by law.\n" );
  }


void show_strerror( const char * const filename, const int errcode )
  {
  if( !scripted_ )
    {
    if( filename && filename[0] ) fprintf( stderr, "%s: ", filename );
    fprintf( stderr, "%s\n", strerror( errcode ) );
    }
  }


static void show_error( const char * const msg, const int errcode, const bool help )
  {
  if( msg && msg[0] )
    fprintf( stderr, "%s: %s%s%s\n", program_name, msg,
             ( errcode > 0 ) ? ": " : "",
             ( errcode > 0 ) ? strerror( errcode ) : "" );
  if( help )
    fprintf( stderr, "Try '%s --help' for more information.\n",
             invocation_name );
  }


/* return true if file descriptor is a regular file */
bool is_regular_file( const int fd )
  {
  struct stat st;
  return ( fstat( fd, &st ) != 0 || S_ISREG( st.st_mode ) );
  }


bool may_access_filename( const char * const name )
  {
  if( restricted_ )
    {
    if( name[0] == '!' )
      { set_error_msg( "Shell access restricted" ); return false; }
    if( strcmp( name, ".." ) == 0 || strchr( name, '/' ) )
      { set_error_msg( "Directory access restricted" ); return false; }
    }
  return true;
  }


int main( const int argc, const char * const argv[] )
  {
  int argind;
  bool initial_error = false;		/* fatal error reading file */
  bool loose = false;
  enum { opt_cr = 256 };
  const struct ap_Option options[] =
    {
    { 'E', "extended-regexp",      ap_no  },
    { 'G', "traditional",          ap_no  },
    { 'h', "help",                 ap_no  },
    { 'H', "highlight",            ap_yes },
    { 'l', "loose-exit-status",    ap_no  },
    { 'p', "prompt",               ap_yes },
    { 'r', "restricted",           ap_no  },
    { 's', "quiet",                ap_no  },
    { 's', "silent",               ap_no  },
    { 'v', "verbose",              ap_no  },
    { 'V', "version",              ap_no  },
    { opt_cr, "strip-trailing-cr", ap_no  },
    {  0, 0,                       ap_no } };

  struct Arg_parser parser;
  if( argc > 0 ) invocation_name = argv[0];

  if( !ap_init( &parser, argc, argv, options, 0 ) )
    { show_error( "Memory exhausted.", 0, false ); return 1; }
  if( ap_error( &parser ) )				/* bad option */
    { show_error( ap_error( &parser ), 0, true ); return 1; }

  for( argind = 0; argind < ap_arguments( &parser ); ++argind )
    {
    const int code = ap_code( &parser, argind );
    const char * const arg = ap_argument( &parser, argind );
    if( !code ) break;					/* no more options */
    switch( code )
      {
      case 'E': extended_regexp_ = true; break;
      case 'G': traditional_ = true; break;	/* backward compatibility */
      case 'h': show_help(); return 0;
      case 'H': if( set_lang( arg ) ) break; else return 1;
      case 'l': loose = true; break;
      case 'p': if( set_prompt( arg ) ) break; else return 1;
      case 'r': restricted_ = true; break;
      case 's': scripted_ = true; break;
      case 'v': set_verbose(); break;
      case 'V': show_version(); return 0;
      case opt_cr: strip_cr_ = true; break;
      default : show_error( "internal error: uncaught option.", 0, false );
                return 3;
      }
    } /* end process options */

  setlocale( LC_ALL, "" );
  if( !init_buffers() ) return 1;

  while( argind < ap_arguments( &parser ) )
    {
    const char * const arg = ap_argument( &parser, argind );
    if( strcmp( arg, "-" ) == 0 ) { scripted_ = true; ++argind; continue; }
    if( may_access_filename( arg ) )
      {
      const int ret = read_file( arg, 0 );
      if( ret < 0 && is_regular_file( 0 ) ) return 2;
      if( arg[0] != '!' && !set_def_filename( arg ) ) return 1;
      if( ret == -2 ) initial_error = true;
      }
    else
      {
      if( is_regular_file( 0 ) ) return 2;
      initial_error = true;
      }
    break;
    }
  ap_free( &parser );

  if( initial_error ) fputs( "?\n", stdout );
  return main_loop( initial_error, loose );
  }
