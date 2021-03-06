/*
Program name: jcblock

File name: jcblock.c

Copyright: 	Copyright 2008 Walter S. Heath

Copy permission:
This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See theGNU General Public License for more details.

You may view the GNU General Public License at: <http://www.gnu.org/licenses/>.

Description:
A program to block telemarketing (junk) calls. This program connects to a serial
port modem and listens for the caller ID string that is sent between the first
and second rings. It records the string in file callerID.dat. It then reads 
strings from file whitelist.dat and scans them against the caller ID string for
a match. If it finds a match it accepts the call. If a match is not found, it
reads strings from file blacklist.dat and scans them against the caller ID
string for a match. If it finds a match to a string in the blacklist,  it sends
modem commands that terminate the junk call. For more details, see README file.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <time.h>

typedef int bool;

#ifndef TRUE
  #define TRUE 1
  #define FALSE 0
#endif

FILE *fpCa;                // callerID.dat file
FILE *fpBl;               // blacklist.dat file

#define OUTPUT_TO_LOG  // will send printf output to jcblock.log

#define OPEN_PORT_BLOCKED 1
#define OPEN_PORT_POLLED  0

#define DEBUG

// Default serial port specifier.
char *serialPort = "/dev/ttyACM0";
int fd;                                  // the serial port

FILE *fpWh;                              // whitelist.dat file
static struct termios options;
static time_t pollTime, pollStartTime;
static bool modemInitialized = FALSE;
static bool inBlockedReadCall = FALSE;
static int numRings;

// Prototypes
static void cleanup( int signo );
int send_modem_command(int fd, char *command );
static bool check_blacklist( char *callstr );
static bool check_whitelist( char * callstr );
static void open_port( int mode );
static void close_open_port();
int init_modem(int fd );
struct timeval start, end;
long mtime, seconds, useconds;    

FILE *stdoutStream ;

static char *copyright = "Running jcblock\n";

//
//  Log a command to the output
//
int log_debug_info(char *command )
{  /* Begin log_debug_info */
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  char iso_8601[] = "YYYY-MM-DDTHH:MM:SS";

#ifdef DEBUG
  gettimeofday(&end, NULL);
  seconds  = end.tv_sec  - start.tv_sec;
  useconds = end.tv_usec - start.tv_usec;
  mtime = ((seconds) * 1000 + useconds/1000.0);

  now_tm = localtime(&now);
  strftime(iso_8601, sizeof (iso_8601), "%FT%R:%S", now_tm);
  printf("%s %12ld msec %s\n",iso_8601,mtime,command) ;
  
#ifdef OUTPUT_TO_LOG
  // Flush anything in stdout (needed if stdout is redirected to a disk file).
  fflush(stdout);     // flush C library buffers to kernel buffers
  sync();             // flush kernel buffers to disk
#endif
#endif
// use start=end ; to time event
}  /* end   log_debug_info */


int log_info(char *command )
{  /* Begin log_info */
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  char iso_8601[] = "YYYY-MM-DDTHH:MM:SS";

  gettimeofday(&end, NULL);
  seconds  = end.tv_sec  - start.tv_sec;
  useconds = end.tv_usec - start.tv_usec;
  mtime = ((seconds) * 1000 + useconds/1000.0);
  now_tm = localtime(&now);
  strftime(iso_8601, sizeof (iso_8601), "%FT%R:%S", now_tm);
  printf("%s %12ld msec %s",iso_8601,mtime,command) ;

#ifdef OUTPUT_TO_LOG
  // Flush anything in stdout (needed if stdout is redirected to a disk file).
  fflush(stdout);     // flush C library buffers to kernel buffers
  sync();             // flush kernel buffers to disk
#endif
}  /* end   log_info */

//
// Main function
//
int main(int argc, char **argv)
{ /* Begin main */
  int optChar;
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  char iso_8601[] = "YYYY-MM-DDTHH:MM:SS";  

  // Set Ctrl-C and kill terminator signal catchers
  signal( SIGINT, cleanup );
  signal( SIGKILL, cleanup );

  gettimeofday(&start, NULL);
  end=start;

  // If jcblock called from cron BE SURE TO USE ABSOLUTE PATHS FOR <ANY> FILES THAT ARE OPENED
#ifdef OUTPUT_TO_LOG
if((stdoutStream = freopen("/home/pi/jcblock/jcblock.log", "a+", stdout)) == NULL)
  exit(-1);
#endif

  // Display copyright notice
  log_info(copyright);

  // Open or create a file to append caller ID strings to
  start=end ;
  if( (fpCa = fopen( "/home/pi/jcblock/callerID.dat", "a+" ) ) == NULL )
  {
    log_debug_info("fopen() of callerID.dat failed");
    return;
  }

  // Open the whitelist file (for reading & writing)
  start=end ;
  if( (fpWh = fopen( "/home/pi/jcblock/whitelist.dat", "r+" ) ) == NULL )
  {
    log_debug_info("fopen() of whitelist.dat failed. A whitelist is not required." );
  }

  // Open the blacklist file (for reading & writing)
  start=end ;
  if( (fpBl = fopen( "/home/pi/jcblock/blacklist.dat", "r+" ) ) == NULL )
  {
    log_debug_info("fopen() of blacklist.dat failed. A blacklist must exist." );
    return;
  }

  // Open the serial port
  open_port( OPEN_PORT_BLOCKED );

  // Initialize the modem
  start=end ;
  if( init_modem( fd ) != 0 )
  {
    log_debug_info("init_modem() failed");
    close(fd);
    fclose(fpCa);
    fclose(fpBl);
    fclose(fpWh);
    fflush(stdout);
#ifdef OUTPUT_TO_LOG
    fclose(stdoutStream) ;
#endif	
    fflush(stdout);
    sync();
    return;
  }

  modemInitialized = TRUE;

  // Wait for calls to come in and process the calls...
  wait_for_response(fd);

  close( fd );
  fclose(fpCa);
  fclose(fpBl);
  fclose(fpWh);
  fflush(stdout);
#ifdef OUTPUT_TO_LOG
  fclose(stdoutStream) ;
#endif	
  sync();
}  /* end main */

//
// Initialize the modem.
//
int init_modem(int fd)
{  /* Begin init_modem */

  // Reset the modem
  start=end ;
  if( send_modem_command(fd, "AT\r") != 0 )
  {
    log_debug_info("send_modem_command AT failed") ;
    return(-1);
  }
  
  if( send_modem_command(fd, "ATZ\r") != 0 )
  {
    log_debug_info("send_modem_command ATZ failed") ;
    return(-1);
  }

  usleep(100000);
  log_debug_info("send modem AT, send modem ATZ, usleep 100000");

  start=end ;
  // Initialize the modem to terminate a call when its serial port DTR line goes
  // inactive. DTR goes inactive when the connection to its serial port is closed.
  // This will be used to terminate a call found on the blacklist
  // (with some modems, "AT&D3\r" may be needed).
  if( send_modem_command(fd, "AT&D2\r") != 0 )
  {
    log_debug_info("send_modem_command AT&D2 failed");
    return(-1);
  }

  // Tell modem to return caller ID Note: different modems use different commands
  // here. If this command hangs the program, try these others: AT#CID=1,  AT#CLS=8#CID=1,
  // AT#CID=2,  AT%CCID=1, AT%CCID=2,  AT#CC1,  AT*ID1 or check modem documentation.
  start=end ;
  if( send_modem_command(fd, "AT+VCID=1\r") != 0 )
  {
    log_debug_info("send_modem_command AT+VCID=1 failed");
    return(-1);
  }
  usleep(100000);
  log_debug_info("send modem AT&DT, send modem AT+VCID=1, usleep 100000");
  
  return(0);
} /* end init_modem */

//
// Send command string to the modem
//
int send_modem_command(int fd, char *command )
{  /* Begin send_modem_command */
  char buffer[255];     // Input buffer
  char *bufptr;         // Current char in buffer
  int nbytes;           // Number of bytes read
  int tries;            // Number of tries so far
  int i;

  // Send command
  if( write(fd, command, strlen(command) ) != strlen(command) )
  {
    log_debug_info("send_modem_command failed" );
  }

  start=end;
  for( tries = 0; tries < 20; tries++ )
  {
    // Read characters into our string buffer until we get a CR or NL
    bufptr = buffer;
    inBlockedReadCall = TRUE;
    while( (nbytes = read(fd, bufptr, buffer + sizeof(buffer) - bufptr - 1)) > 0 )
    {
      bufptr += nbytes;
      if( bufptr[-1] == '\n' || bufptr[-1] == '\r' )
        break;
    }
    inBlockedReadCall = FALSE;

    // Null terminate the string and see if we got an OK response
    *bufptr = '\0';

    // Scan for string "OK"
    if( strstr( buffer, "OK" ) != NULL )
    {
      return( 0 );
    }
  }
  log_debug_info("did not get command OK");
  return( -1 );
}  /* end send_modem_command */


//
// Wait (forever!) for calls...
//
int wait_for_response(fd)
{ //Begin of wait_for_response
  char buffer[255];     // Input buffers
  char callerIDentry[255];
  char bufferString[128];
  int nbytes;           // Number of bytes read
  int i, j;
  int  cch = '-';
  char *callID, *callNumber ;
  char *p1callID, *p2callID ;
  char *p1N, *p2N ;

  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  char iso_8601[] = "YYYY-MM-DDTHH:MM:SS";  

  log_info("Waiting for a call ...\n") ;

  // Get a string of characters from the modem
  while(1)
  {
    start=end ;
    // Flush anything in stdout (needed if stdout is redirected to a disk file).
    fflush(stdout);     // flush C library buffers to kernel buffers
    sync();             // flush kernel buffers to disk
    log_info("Waiting for modem event ...\n") ;

    // Block until at least one character is available. After first character is
    // received, continue reading characters until inter-character timeout
    // (VTIME) occurs (or VMIN characters are received, which shouldn't happen,
    // since VMIN is set larger than the longest string expected).

    inBlockedReadCall = TRUE;
    nbytes = read( fd, buffer, 250 );
    inBlockedReadCall = FALSE;
    start=end ;

    sprintf(bufferString,"received %d buffer bytes",nbytes) ;
    log_debug_info(bufferString);

    // Occasionally a call comes in that has a caller ID field that is too long
    // Truncate it to the standard length (15 chars):
    if( nbytes > 81 )
    {
      nbytes = 81;
      buffer[79] = '\r';
      buffer[80] = '\n';
      buffer[81] = 0;
      log_debug_info("Truncated buffer because > 81 bytes");
    }
    start=end;

    // Replace '\n' and '\r' characters with '-' characters
    for( i = 0; i < nbytes; i++ )
    {
      if( ( buffer[i] == '\n' ) || ( buffer[i] == '\r' ) )
      {
        buffer[i] = '-';
      }
    }

    // Put a '\n' at buffer end and null-terminate it
    buffer[nbytes] = '\n';
    buffer[nbytes + 1] = 0;
    log_debug_info(buffer) ;

    // A string was received. If its a 'RING' string, just ignore it.
    if( strstr( buffer, "RING" ) != NULL )
    {
      continue;
    }

    // Ignore a string "AT+VCID=1" returned from the modem.
    if( strncmp( buffer, "AT+VCID=1", 9 ) == 0 )
    {
      continue;
    }

    // Caller ID data was received after the first ring.
    numRings = 1;

    //  Create a caller ID string

    // Create callTime from current time
    now = time(NULL);
    now_tm = localtime(&now);
    strftime(iso_8601, sizeof (iso_8601), "%FT%R", now_tm);

    const char *NAME = strstr(buffer, "NAME")+7;
    const char *NMBR = strstr(buffer, "NMBR")+7;
    size_t p1 = NAME-NMBR;
    size_t p2 = NMBR-NAME;
    
/*  There are different caller ID strings with different order
--DATE = 1217--TIME = 1650--NMBR = 12345678901--NAME = WIRELESS CALLER--
--DATE = 1217--TIME = 2211--NAME = WIRELESS CALLER--NMBR = 12345678902--
*/

    if ((int)p1 < (int)p2) // --DATE = 1217--TIME = 2211--NAME = WIRELESS CALLER--NMBR = 12345678901--
    {
      p1callID = strstr(buffer, "NAME")+7;
      p2callID = strstr(buffer, "NMBR")-2;
      p1N = strstr(buffer, "NMBR")+7;
      p2N = strrchr(buffer, cch)-1;
    }
    else //--DATE = 1217--TIME = 1650--NMBR = 17346349060--NAME = WIRELESS CALLER-
    {
      p1callID = strstr(buffer, "NAME")+7;
      p2callID = strrchr(buffer, cch)-1;
      p1N = strstr(buffer, "NMBR")+7;
      p2N = strstr(buffer, "NAME")-2;
    }
    // set the caller ID name string callID and terminate it
    size_t lencallID = p2callID-p1callID;
    callID = (char*)malloc(sizeof(char)*(lencallID+1));
    strncpy(callID, p1callID, lencallID);
    callID[lencallID] = '\0';

    // set the caller number string callNumber and terminate it
    size_t lenN = p2N-p1N;
    callNumber = (char*)malloc(sizeof(char)*(lenN+1));
    strncpy(callNumber, p1N, lenN);
    callNumber[lenN] = '\0';

    // set the callIDentry, put '\n' at end and null-terminate it    
    sprintf(callerIDentry,"%s|%s|%s|",iso_8601,callNumber,callID) ;
    size_t lenID = strlen(callerIDentry);
    callerIDentry[lenID] = '\n';
    callerIDentry[lenID + 1] = 0;
    log_info( callerIDentry );

    // Close and re-open file 'callerID.dat' (in case it was
    // edited while the program was running!).
    start=end ;
    fclose(fpCa);
    if( (fpCa = fopen( "/home/pi/jcblock/callerID.dat", "a+" ) ) == NULL )
    {
      log_debug_info("re-fopen() of callerID.dat failed");
      return(-1);
    }

    // Write the record to the file
    start=end ;
    if( fputs( (const char *)callerIDentry, fpCa ) == EOF )
    {
      log_debug_info("fputs( (const char *)callerIDentry, fpCa ) failed");
      return(-1);
    }

    // Flush the record to the file
    start=end ;
    if( fflush(fpCa) == EOF )
    {
      log_debug_info("fflush(fpCa) failed");
      return(-1);
    }

    // If a whitelist.dat file was present, compare the caller ID string to entries
    // in the whitelist. If a match is found, accept call and bypass blacklist check
    if( fpWh != NULL )
    {
      if( check_whitelist( callerIDentry ) == TRUE )
      {
        // Caller ID match was found (or an error occurred), so accept the call
        continue;
      }
    }

    // Compare the caller ID string to entries in the blacklist. If
    // a match is found, answer (i.e., terminate) the call.
    if( check_blacklist( callerIDentry ) == TRUE )
    {
      // Blacklist entry was found. //
      continue;
    }
    fflush(stdout);
  }
} // End of wait_for_response

//
// Compare strings in the 'whitelist.dat' file to fields in the received caller ID string. 
// If a whitelist string is present (or an error occurred), return TRUE; otherwise return FALSE.
//
static bool check_whitelist( char *callstr )
{  /* Begin check_whitelist */
  char whitebuf[100];
  char whitebufsave[100];
  char whitelistMessage[256];
  char *whitebufptr;
  char call_date[18];
  char *dateptr;
  char *strptr;
  int i;
  long file_pos_last, file_pos_next;

  // Close and re-open the whitelist.dat file. Note: this seems to be necessary 
  // to be able to write records back into the file. The write works the first
  // time after the file is opened but not subsequently! :-( This also allows
  // whitelist changes made while the program is running to be recognized.
  fclose( fpWh );

  // Re-open for reading and writing
  start=end;
  if( (fpWh = fopen( "/home/pi/jcblock/whitelist.dat", "r+" ) ) == NULL )
  {
    log_debug_info("Re-open of whitelist.dat file failed" );
    return(TRUE);           // accept the call
  }

  // Disable buffering for whitelist.dat writes
  setbuf( fpWh, NULL );

  // Seek to beginning of list
  fseek( fpWh, 0, SEEK_SET );

  // Save the file's current access location
  start=end;
  if( file_pos_next = ftell( fpWh ) == -1L )
  {
    log_debug_info("ftell(fpWh) failed");
    return(TRUE);           // accept the call
  }

  // Read and process records from the file
  while( fgets( whitebuf, sizeof( whitebuf ), fpWh ) != NULL )
  {
    // Save the start location of the string just read and get
    // the location of the start of the next string in the file.
    file_pos_last = file_pos_next;
    file_pos_next = ftell( fpWh );

    // Ignore lines that start with a '#' character (comment lines)
    if( whitebuf[0] == '#' )
      continue;

    // Ignore lines containing just a '\n'
    if( whitebuf[0] == '\n' )
    {
      continue;
    }

    // Ignore records that are too short (don't have room for the date)
    if( strlen( whitebuf ) < 26 )
    {
      log_info("\nERROR: whitelist.dat record is too short to hold date field.\n");
      log_info(whitebuf);
      log_info("record is ignored (edit file and fix it).\n");
      continue;
    }

    // Save the string (for writing back to the file later)
    strcpy( whitebufsave, whitebuf );

    // Make sure a '?' char is present in the string
    if( ( strptr = strstr( whitebuf, "?" ) ) == NULL )
    {
      log_info("\nERROR: all whitelist.dat entry first fields *must be*\n");
      log_info("terminated with a \'?\' character!! Entry is:\n");
      log_info(whitebuf);
      log_info("Entry was ignored!\n");
      continue;
    }

    // Make sure the '?' character is within the first twenty characters
    if( (int)( strptr - whitebuf ) > 18 )
    {
      log_info("\nERROR: terminator '?' is not within first 20 characters\n" );
      log_info(whitebuf);
      log_info("Entry was ignored!\n");
      continue;
    }

    // Get a pointer to the search token in the string
    if( ( whitebufptr = strtok( whitebuf, "?" ) ) == NULL )
    {
      log_debug_info("whitebuf strtok() failed");
      return(TRUE);         // accept the call
    }

    // Scan the call string for the whitelist entry
    if( strstr( callstr, whitebufptr ) != NULL )
    {
      sprintf(whitelistMessage,"*** whitelist match on: %s ***\n",whitebuf) ;
      log_info(whitelistMessage) ;

      // Get the current timestsamp from the caller ID string
      strncpy( call_date, &callstr[0], 16 );

      // Terminate the string
      call_date[16] = 0;

      // Update the date in the whitebufsave record
      strncpy( &whitebufsave[20], call_date, 16 );

      // Write the record back to the whitelist.dat file
      fseek( fpWh, file_pos_last, SEEK_SET );
      if( fputs( whitebufsave, fpWh ) == EOF )
      {
        log_debug_info("fputs(whitebufsave, fpWh) failed" );
        return(TRUE);         // accept the call
      }

      // Flush the string to the file
      if( fflush(fpWh) == EOF )
      {
        log_debug_info("fflush(fpWh) failed");
        return(TRUE);         // accept the call
      }

      fflush(stdout);
      sync();
      // A whitelist.dat entry matched, so return TRUE
      return(TRUE);             // accept the call
    }
  }                               // end of while()

  // No whitelist.dat entry matched, so return FALSE.
  return(FALSE);
}  /* end check_whitelist */

//
// Compare strings in the 'blacklist.dat' file to fields in the received caller 
// ID string. If a blacklist string is present, send off-hook (ATH1) and
// on-hook (ATH0) to the modem to terminate the call...
//
static bool check_blacklist( char *callstr )
{  /* Begin check_blacklist */
  char blackbuf[100];
  char blackbufsave[100];
  char blacklistMessage[256];
  char *blackbufptr;
  char call_date[18];
  char *dateptr;
  char *strptr;
  int i;
  long file_pos_last, file_pos_next;
  char yearStr[10];

  // Close and re-open the blacklist.dat file. Note: this seems to be necessary
  // to be able to write records back into the file. The write works the first
  // time after the file is opened but not subsequently! :-( This also allows
  // blacklist changes made while the program is running to be recognized.
  fclose( fpBl );
  // Re-open for reading and writing
  if( (fpBl = fopen( "/home/pi/jcblock/blacklist.dat", "r+" ) ) == NULL )
  {
    log_debug_info("re-open fopen( blacklist) failed" );
    return(FALSE);
  }

  // Disable buffering for blacklist.dat writes
  setbuf( fpBl, NULL );

  // Seek to beginning of list
  fseek( fpBl, 0, SEEK_SET );

  // Save the file's current access location
  if( file_pos_next = ftell( fpBl ) == -1L )
  {
    log_debug_info("ftell(fpBl) failed");
    return(FALSE);
  }

  start=end;
  // Read and process records from the file
  while( fgets( blackbuf, sizeof( blackbuf ), fpBl ) != NULL )
  {
    // Save the start location of the string just read and get
    // the location of the start of the next string in the file.
    file_pos_last = file_pos_next;
    file_pos_next = ftell( fpBl );

    // Ignore lines that start with a '#' character (comment lines)
    if( blackbuf[0] == '#' )
      continue;

    // Ignore lines containing just a '\n'
    if( blackbuf[0] == '\n' )
    {
      continue;
    }

    // Ignore records that are too short (don't have room for the date)
    if( strlen( blackbuf ) < 26 )
    {
      log_info("\nERROR: blacklist.dat record is too short to hold date field.\n");
      log_info( blackbuf );
      log_info("record is ignored (edit file and fix it).\n");
      continue;
    }

    // Save the string (for writing back to the file later)
    strcpy( blackbufsave, blackbuf );

    // Make sure a '?' char is present in the string
    if( ( strptr = strstr( blackbuf, "?" ) ) == NULL )
    {
      log_info("\nERROR: all blacklist.dat entry first fields *must be*\n");
      log_info("       terminated with a \'?\' character!! Entry is:\n");
      log_info(blackbuf);
      log_info("Entry was ignored!\n");
      continue;
    }

    // Make sure the '?' character is within the first twenty characters
    // (could not be if the previous record was only partially written).
    if( (int)( strptr - blackbuf ) > 18 )
    {
      log_info("ERROR: terminator '?' is not within first 20 characters\n" );
      log_info(blackbuf);
      log_info("Entry was ignored!\n");
      continue;
    }

    // Get a pointer to the search token in the string
    if( ( blackbufptr = strtok( blackbuf, "?" ) ) == NULL )
    {
      log_debug_info("blackbuf strtok() failed");
      return(FALSE);
    }

    // Scan the call string for the blacklist entry or if the caller ID string
    // is less than 23 characters in length (number is one character and caller
    // ID string is one in length)
    // 2013-10-01T19:12|1|O| = 21 characters in length
    if( (strstr( callstr, blackbufptr ) != NULL ) || (strlen(callstr) < 23))
    {
      sprintf(blacklistMessage,"***  blacklist match on: %s ***\n",blackbuf) ;
      log_info(blacklistMessage) ;

      // At this point, the modem is in data mode. It must be returned to command mode to
      // send it the off-hook and on-hook commands.  Close, open and reinitialize the
      // connection. This clears the DTR line which resets the modem to command
      // mode. To accomplish this in time (before the next ring), the caller ID
      // command is not sent. Later, the modem is again reinitialized with
      // caller ID activated. This is all kind of crude, but it works...
      // Terminate the call by closing the modem serial port. and by sending off hook and
      // on hook commands Then re-open it andre-initialize the modem to prepare for the next call.
      start=end;
      usleep( 100000 );
      send_modem_command(fd, "ATH1\r"); // off hook
      usleep( 250000 );    // quarter second
      send_modem_command(fd, "ATH0\r"); // on hook
      usleep( 250000 );    // quarter second
      log_debug_info("usleep 100000, send ATH1, usleep 250000, send ATH0, usleep 250000");

      start=end;  
      close_open_port( );
      usleep( 250000 );
      log_debug_info("close_open_port(), usleep 250000" ) ;

      // Get the current date from the caller ID string
      strncpy( call_date, &callstr[0], 16 );

      // Terminate the string
      call_date[16] = 0;

      // Update the date in the blackbufsave record
      strncpy( &blackbufsave[20], call_date, 16 );

      // Write the record back to the blacklist.dat file
      start=end;
      fseek( fpBl, file_pos_last, SEEK_SET );
      if( fputs( blackbufsave, fpBl ) == EOF )
      {
        log_debug_info("fputs(blackbufsave, fpBl) failed" );
        return(FALSE);
      }

      // Flush the string to the file
      start=end;
      if( fflush(fpBl) == EOF )
      {
        log_debug_info("fflush(fpBl) failed");
        return(FALSE);
      }

      // Force kernel file buffers to the disk
      // (probably not necessary)
      sync();

      // A blacklist.dat entry matched, so return TRUE
      fflush(stdout);
      start=end;
      return(TRUE);
    }
  }  // end of while()

  /* A blacklist.dat entry was not matched, so return FALSE */
  return(FALSE);
}  /* end check_blacklist */

//
// Open the serial port.
//
static void open_port(int mode )
{  /* Begin open_port */
  // Open modem device for reading and writing and not as the controlling
  // tty (so the program does not get terminated if line noise sends CTRL-C).
  //
  start=end;
  if( ( fd = open( serialPort, O_RDWR | O_NOCTTY ) ) < 0 )
  {
    perror( serialPort );
    log_debug_info("failed to open serial port") ;
    _exit(-1);
  }
  fcntl(fd, F_SETFL, 0);

  // Get the current options
  tcgetattr(fd, &options);

  // Set eight bits, no parity, one stop bit
  options.c_cflag       &= ~PARENB;
  options.c_cflag       &= ~CSTOPB;
  options.c_cflag       &= ~CSIZE;
  options.c_cflag       |= CS8;

  // Set hardware flow control
  options.c_cflag       |= CRTSCTS;

  // Set raw input
  options.c_cflag       |= (CLOCAL | CREAD);

  options.c_lflag       &= ~(ICANON | ECHO |ECHOE | ISIG);
  options.c_oflag       &=~OPOST;

  if( mode == OPEN_PORT_BLOCKED )
  {
    // Block read until a character is available or inter-character
    // time exceeds 1 unit (in 0.1sec units)
    options.c_cc[VMIN]    = 80;
    options.c_cc[VTIME]   = 1;
  }
  else                   // (mode == OPEN_PORT_POLLED)
  {
    // A read returns immediately with up to the number of bytes
    // requested. It returns the number read; zero if none available
    options.c_cc[VMIN]    = 0;
    options.c_cc[VTIME]   = 0;
  }

  // Set the baud rate (caller ID is sent at 1200 baud)
  cfsetispeed( &options, B1200 );
  cfsetospeed( &options, B1200 );

  // Set options
  tcsetattr(fd, TCSANOW, &options);
}  /* end open_port */

//
// Function to close and open the serial port to disable the DTR
// line. Needed to switch the modem from data mode back into command mode.
//
static void close_open_port()
{  /* Begin close_open_port */
  // Close the port
  close(fd);
  start=end;
  usleep( 250000 );   // quarter second
  open_port( OPEN_PORT_BLOCKED );
  usleep( 250000 );   // quarter second
  log_debug_info("usleep(250000), open port, usleep 250000") ;
  init_modem(fd );
} /* end close_open_port */

//
// SIGINT (Ctrl-C) and SIGKILL signal handler
//
static void cleanup( int signo )
{ /* Begin cleanup */
  start=end;
  log_debug_info("in cleanup()...wait for kill...");

  if( modemInitialized )
  {
    // Reset the modem
    send_modem_command(fd, "ATZ\r");
    log_debug_info("sent ATZ command...\n");
  }

  // Close everything
  close(fd);
  fclose(fpCa);
  fclose(fpBl);
  fclose(fpWh);
  fflush(stdout);     // flush C library buffers to kernel buffers
#ifdef OUTPUT_TO_LOG
  fclose(stdoutStream) ;
#endif	  
  sync();             // flush kernel buffers to disk

  // If program is in a blocked read(...) call, use kill() to
  // terminate program (happens when modem is not connected!).
  if( inBlockedReadCall )
  {
    kill( 0, SIGKILL );
  }
  log_info("\n\nProgram Terminated\n\n") ;
  // Otherwise terminate normally
  _exit(0);
} /* Begin cleanup */

