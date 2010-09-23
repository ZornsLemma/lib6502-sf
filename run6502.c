/* run6502.c -- 6502 emulator shell			-*- C -*- */

/* Copyright (c) 2005 Ian Piumarta
/* BBC 6502 second processor emulation (c) 2010 Steven Flintham
 * 
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the 'Software'),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, provided that the above copyright notice(s) and this
 * permission notice appear in all copies of the Software and that both the
 * above copyright notice(s) and this permission notice appear in supporting
 * documentation.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS'.  USE ENTIRELY AT YOUR OWN RISK.
 */

/* Last edited: 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "lib6502.h"

#define VERSION	PACKAGE_NAME " " PACKAGE_VERSION " " PACKAGE_COPYRIGHT

typedef uint8_t  byte;
typedef uint16_t word;

static char *program= 0;

static byte bank[0x10][0x4000];

static char *tube_command= 0;

static int exit_write= 0;
static M6502 *exit_write_mpu= 0;




void fail(const char *fmt, ...)
{
  va_list ap;
  fflush(stdout);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  exit(1);
}


void pfail(const char *msg)
{
  fflush(stdout);
  perror(msg);
  exit(1);
}


#define rts							\
  {								\
    word pc;							\
    pc  = mpu->memory[++mpu->registers->s + 0x100];		\
    pc |= mpu->memory[++mpu->registers->s + 0x100] << 8;	\
    return pc + 1;						\
  }


int oswordCommon(M6502 *mpu, word address, byte data)
{
  byte *params= mpu->memory + mpu->registers->x + (mpu->registers->y << 8);

  switch (mpu->registers->a)
    {
    case 0x00: /* input line */
      /* On entry: XY+0,1=>string area,
       *	   XY+2=maximum line length,
       *	   XY+3=minimum acceptable ASCII value,
       *	   XY+4=maximum acceptable ASCII value.
       * On exit:  Y is the line length (excluding CR),
       *	   C is set if Escape terminated input.
       */
      {
	word  offset= params[0] + (params[1] << 8);
	byte *buffer= mpu->memory + offset;
	byte  length= params[2], minVal= params[3], maxVal= params[4], b= 0;
	if (!fgets(buffer, length, stdin))
	  {
	    putchar('\n');
	    exit(0);
	  }
	mpu->registers->p &= 0xFE;
	for (b= 0;  b < length;  ++b)
          {
            if (buffer[b] == 0x1b)
              {
                mpu->registers->p |= 1;
                break;
              }
	    if ((buffer[b] < minVal) || (buffer[b] > maxVal) || ('\n' == buffer[b]))
	      break;
          }
	buffer[b]= 13;
	mpu->registers->y= b;
	break;
      }

    default:
      {
	char state[64];
	M6502_dump(mpu, state);
	fflush(stdout);
	fprintf(stderr, "\nOSWORD %s\n", state);
	fail("ABORT");
      }
      break;
    }
}

  
int osword(M6502 *mpu, word address, byte data)
{
  oswordCommon(mpu, address, data);
  rts;
}


int osbyte(M6502 *mpu, word address, byte data)
{
  switch (mpu->registers->a)
    {
    case 0x7A:	/* perform keyboard scan */
      mpu->registers->x= 0x00;
      break;

    case 0x7E:	/* acknowledge detection of escape condition */
      return 1;
      break;

    case 0x82:	/* read machine higher order address */
      mpu->registers->y= 0x00;
      mpu->registers->x= 0x00;
      break;

    case 0x83:	/* read top of OS ram address (OSHWM) */
      mpu->registers->y= 0x0E;
      mpu->registers->x= 0x00;
      break;

    case 0x84:	/* read bottom of display ram address */
      mpu->registers->y= 0x80;
      mpu->registers->x= 0x00;
      break;

    case 0x89:	/* motor control */
      break;

    case 0xDA:	/* read/write number of items in vdu queue (stored at 0x026A) */
      return 0;
      break;

    default:
      {
	char state[64];
	M6502_dump(mpu, state);
	fflush(stdout);
	fprintf(stderr, "\nOSBYTE %s\n", state);
	fail("ABORT");
      }
      break;
    }

  rts;
}


static char *getYXString(M6502 *mpu)
{
  byte *params= mpu->memory + mpu->registers->x + (mpu->registers->y << 8);
  static char string[1024];
  char *ptr= string;
  while (13 != *params)
    *ptr++= *params++;
  *ptr= '\0';
  return string;
}


static char *getYXStringOscli(M6502 *mpu)
{
  byte *params= mpu->memory + mpu->registers->x + (mpu->registers->y << 8);
  static char command[1024];
  char *ptr= command;
  while (('*' == *params) || (' ' == *params))
    ++params;
  while (13 != *params)
    *ptr++= *params++;
  *ptr= '\0';
  return command;
}

int oscli(M6502 *mpu, word address, byte data)
{
  char *command= getYXStringOscli(mpu);
  system(command);
  rts;
}


static int oswrchCommon(M6502 *mpu, word address, byte data)
{
  switch (mpu->registers->a)
    {
    case 0x0C:
      fputs("\033[2J\033[H", stdout);
      break;

    default:
      putchar(mpu->registers->a);
      break;
    }
  fflush(stdout);
  return 0;
}


int oswrch(M6502 *mpu, word address, byte data)
{
  oswrchCommon(mpu, address, data);
  rts;
}


static int writeROM(M6502 *mpu, word address, byte value)
{
  return 0;
}


static int bankSelect(M6502 *mpu, word address, byte value)
{
  memcpy(mpu->memory + 0x8000, bank[value & 0x0F], 0x4000);
  return 0;
}


static int doBtraps(int argc, char **argv, M6502 *mpu)
{
  unsigned addr;

  /* Acorn Model B ROM and memory-mapped IO */

  for (addr= 0x8000;  addr <= 0xFBFF;  ++addr)  mpu->callbacks->write[addr]= writeROM;
  for (addr= 0xFC00;  addr <= 0xFEFF;  ++addr)  mpu->memory[addr]= 0xFF;
  for (addr= 0xFE30;  addr <= 0xFE33;  ++addr)  mpu->callbacks->write[addr]= bankSelect;
  for (addr= 0xFE40;  addr <= 0xFE4F;  ++addr)  mpu->memory[addr]= 0x00;
  for (addr= 0xFF00;  addr <= 0xFFFF;  ++addr)  mpu->callbacks->write[addr]= writeROM;

  /* anything already loaded at 0x8000 appears in bank 0 */

  memcpy(bank[0x00], mpu->memory + 0x8000, 0x4000);

  /* fake a few interesting OS calls */

# define trap(vec, addr, func)   mpu->callbacks->call[addr]= (func)
  trap(0x020C, 0xFFF1, osword);
  trap(0x020A, 0xFFF4, osbyte);
//trap(0x0208, 0xFFF7, oscli );	/* enable this to send '*COMMAND's to system(3) :-) */
  trap(0x020E, 0xFFEE, oswrch);
  trap(0x020E, 0xE0A4, oswrch);	/* NVWRCH */
#undef trap

  return 0;
}


/* This array is automatically initialised to 0 as it's static. Element 0 is
 * never used (0 is not a valid BBC file handle) but it's easier not to have to
 * subtract one all the time. */
/* TODO: All the F*D* references in the code should really be F*H* I suppose.
 * (File handle not descriptor) */
/* TODO: Should this be at the top of the code with other global data? */
static FILE *fd_array[256]; /* initialised to 0 as it's static */


static int getFreeBbcFd()
{
  int bbc_fd;
  for (bbc_fd= 1;  bbc_fd <= 255;  ++bbc_fd)
    if (fd_array[bbc_fd] == 0)
      return bbc_fd;
  return 0;
}


static void associateBbcFdAndHostFile(int bbc_fd, FILE *host_file)
{
  fd_array[bbc_fd]= host_file;
}


static FILE *getHostFileForBbcFd(int bbc_fd)
{
  return fd_array[bbc_fd];
}


static void freeBbcFd(int bbc_fd)
{
  fd_array[bbc_fd]= 0;
}


static int tubeOscli(M6502 *mpu, word address, byte value)
{
  const char *error= "Bad command";
  size_t error_length= strlen(error);
  char *command= getYXString(mpu);
  fprintf(stderr, "TODO OSCLI: '%s'\n", command);

  mpu->memory[0x100]= 0x00; /* BRK */
  mpu->memory[0x101]= 254;
  memcpy(mpu->memory + 0x102, error, error_length + 1); /* +1 as we want the NUL terminator */
  return 0x100;
}


static int tubeOsbyte(M6502 *mpu, word address, byte value)
{
  switch (mpu->registers->a)
    {
      case 0xA3:
        if (mpu->registers->x == 243) 
	  {
            if (mpu->registers->y == 6)
              {
  	        /* http://beebwiki.jonripley.com/OSBYTE_%26A3 says this occurs on Tube 
                 * reset to ask for a * command to execute.
                 */
	        if (tube_command)
                  {
		    strcpy(mpu->memory + 0x800, tube_command);
		    strcat(mpu->memory + 0x800, "\r");
		    mpu->registers->y = 0x08;
		    mpu->registers->x = 0x00;
                  }
                else
             	  mpu->registers->y= 0;
                return 0;
              }
            else if (mpu->registers->y == 4)
              {
                /* Same reference; this occurs in some other cases too but we're not
                 * interested.
                 */
            	mpu->registers->y= 0;
                return 0;
              }
          }
    }

    char state[64];
    M6502_dump(mpu, state);
    fflush(stdout);
    fprintf(stderr, "\nUnsupported OSBYTE %02X: %s\n", mpu->registers->a, state);
    /* Carry on; an unsupported OSBYTE is not necessarily a problem, it can happen 
     * on a real machine. We set X to 0xFF.
     */
    mpu->registers->x= 0xFF;

    return 0;
}


static int tubeOsword(M6502 *mpu, word address, byte value)
{
  switch (mpu->registers->a)
    {
      case 0:
        /* TODO: Use -B's version for now. Ideally I would find some way to use readline/editline. */
	/* TODO: -B's version is pretty naff. It can't return full-length due to use of
	 * the buffer in-place, any characters you enter past the limit remain in the
	 * input stream and get taken next time input is required. I am not going to try
	 * and fix it as I hope to use readline/editline and sidestep it, but this note is
	 * just to keep track of its problems in the meantime. */
        oswordCommon(mpu, address, value);
        return 0;
    }

    char state[64];
    M6502_dump(mpu, state);
    fflush(stdout);
    fprintf(stderr, "\nUnsupported OSWORD %02X: %s\n", mpu->registers->a, state);
    /* Carry on. TODO: What does a real machine do in this case? */

    return 0;
}


static int tubeOsrdch(M6502 *mpu, word address, byte value)
{
  /* TODO: Very crude. Would be good to work getline support in. Perhaps ideally a
   * command line option would optionally select a mode where we can read single
   * keypresses. For that matter, it might also be nice to do a curses mode with
   * basic terminal emulation.
   */
  int c= getchar();
  if (c == EOF)
    exit(0);
  mpu->registers->a= c;
  return 0;
}


static int tubeOsbget(M6502 *mpu, word address, byte value)
{
  FILE *host_file= getHostFileForBbcFd(mpu->registers->y);
  if (host_file == 0)
  {
    /* TODO: I suspect we should raise an OS error ("Channel"). For now we just
     * return with C set to indicate an error. */
    mpu->registers->p |= 1;
    return 0;
  }

  int c= getc(host_file);
  if (c == EOF)
  {
    /* TODO: We should probably raise an OS error if this is an error not just
     * EOF. For now we treat both the same. */
    mpu->registers->p |= 1; /* TODO: Not just here, we need a macro or helper
			       fn for this */
    return 0;
  }
  
  mpu->registers->a= c;
  mpu->registers->p &= 0xFE;
  return 0;
}


static int tubeOsfindClose(int bbc_fd)
{
  FILE *host_file= getHostFileForBbcFd(bbc_fd);
  if (host_file == 0)
  {
    /* TODO: I suspect we should raise an OS error ("Channel"). For now we just
     * return silently. */
    return 0;
  }

  if (fclose(host_file) == EOF)
  {
    /* TODO: I suspect we should raise an OS error. For now we just return
     * silently. */
    /* TODO: Also, should we leave the FD allocated on the BBC side? I suspect
     * that's best, as it's vaguely conceivable it can/will then be re-closed
     * successfully later. */
    return 0;
  }

  freeBbcFd(bbc_fd);
  return 0;
}


static int tubeOsfindOpen(M6502 *mpu, const char *mode)
{
  int bbc_fd= getFreeBbcFd();
  FILE *host_file= 0;

  if (bbc_fd == 0)
    {
      /* TODO: I suspect we should raise an OS error. For now we just return with A=0. */
      mpu->registers->a= 0;
      return 0;
    }
  
  host_file= fopen(getYXString(mpu), mode);
  if (host_file == 0)
  {
    /* TODO: I suspect (though it's far from clear) we should raise an OS
     * error. For now just return with A=0. */
    mpu->registers->a= 0;
    return 0;
  }

  associateBbcFdAndHostFile(bbc_fd, host_file);
  mpu->registers->a= bbc_fd;
  return 0;
}


static int tubeOsfind(M6502 *mpu, word address, byte value)
{
  int bbc_fd;

  switch (mpu->registers->a)
    {
    case 0x00:	/* close file */
      bbc_fd= mpu->registers->y;
      if (bbc_fd != 0)
	return tubeOsfindClose(bbc_fd);
      else
      {
	/* TODO: CLOSE ALL OPEN FILES - NOT SURE WHAT HAPPENS IF ONE OF THEM FAILS */
	fprintf(stderr, "\nTODO: Close all files support\n");
      }
      break;

    case 0x40:  /* open file for input */
      return tubeOsfindOpen(mpu, "rb");
      break;

    case 0x80:  /* open file for output */
      return tubeOsfindOpen(mpu, "wb");

    case 0xc0:	/* open file for update */
      return tubeOsfindOpen(mpu, "r+b");

    default:
      {
	/* TODO: Code like this occurs a lot, factor it out */
	char state[64];
	M6502_dump(mpu, state);
	fflush(stdout);
	fprintf(stderr, "\nUnsupported OSFIND %02X: %s\n", mpu->registers->a, state);

	/* TODO: Not necessarily best option (what would a real machine do? is it
	 * well-defined?) but returning with A=0 is a reasonably safe response. */
	mpu->registers->a= 0;
	return 0;
      }
   }
}


static int tubeQuit(M6502 *mpu, word address, byte value)
{
  exit(0);
}


static int tubeEnterLanguage(M6502 *mpu, word address, byte value)
{
     /* TODO: We could probably poll the sideways ROMs for a language and copy that across. 
      * Would need to find how to distinguish call with ROM number in X (OSBYTE 142) from 
      * call made by *BASIC. 
      */
    char state[64];
    M6502_dump(mpu, state);
    fflush(stdout);
    fprintf(stderr, "\nUnsupported enter language call: %s\n", state);
    fail("ABORT");

    return 0;
}


static int doTtraps(int argc, char **argv, M6502 *mpu)
{
  unsigned addr;
  const char *signature= "Acorn 6502 Tube";
  size_t signature_length= strlen(signature);
  int found;

  /* The tube emulation requires the 2K ROM from 65Tube to be loaded at 0xF800. Refuse
   * to continue if something like it isn't there. To allow for variations, we just
   * check for a certain string somewhere in the right area.
   */
  found= 0;
  for (addr= 0xF800;  addr <= (0x10000 - signature_length);  ++addr)
    if (!memcmp(mpu->memory + addr, signature, signature_length))
      {
        found= 1; 
	break;
      }
  if (!found)
    fail("-T requires Tube emulation ROM to be loaded");

  /* On real hardware the ROM is copied into RAM on startup; all 64K is writeable. So
   * we don't need to write-protect anything. */

  M6502_setCallback(mpu, illegal_instruction, 0x03, tubeOscli);
  M6502_setCallback(mpu, illegal_instruction, 0x13, tubeOsbyte);
  M6502_setCallback(mpu, illegal_instruction, 0x23, tubeOsword);
  M6502_setCallback(mpu, illegal_instruction, 0x33, oswrchCommon);
  M6502_setCallback(mpu, illegal_instruction, 0x43, tubeOsrdch);
  M6502_setCallback(mpu, illegal_instruction, 0x73, tubeOsbget);
  M6502_setCallback(mpu, illegal_instruction, 0xA3, tubeOsfind);
  M6502_setCallback(mpu, illegal_instruction, 0xB3, tubeQuit);
  M6502_setCallback(mpu, illegal_instruction, 0xC3, tubeEnterLanguage);

  return 0;
}


static void usage(int status)
{
  FILE *stream= status ? stderr : stdout;
  fprintf(stream, VERSION"\n");
  fprintf(stream, "please send bug reports to: %s\n", PACKAGE_BUGREPORT);
  fprintf(stream, "\n");
  fprintf(stream, "usage: %s [option ...]\n", program);
  fprintf(stream, "       %s [option ...] -B [image ...]\n", program);
  fprintf(stream, "  -B                -- minimal Acorn 'BBC Model B' compatibility\n");
  fprintf(stream, "  -c                -- next argument is command to run on Tube startup\n"); /* TODO: This is not documented in run6502.1 */
  fprintf(stream, "  -d addr last      -- dump memory between addr and last\n");
  fprintf(stream, "  -G addr           -- emulate getchar(3) at addr\n");
  fprintf(stream, "  -h                -- help (print this message)\n");
  fprintf(stream, "  -I addr           -- set IRQ vector\n");
  fprintf(stream, "  -l addr file      -- load file at addr\n");
  fprintf(stream, "  -M addr           -- emulate memory-mapped stdio at addr\n");
  fprintf(stream, "  -N addr           -- set NMI vector\n");
  fprintf(stream, "  -P addr           -- emulate putchar(3) at addr\n");
  fprintf(stream, "  -R addr           -- set RST vector\n");
  fprintf(stream, "  -s addr last file -- save memory from addr to last in file\n");
  fprintf(stream, "  -T                -- Acorn 6502 Tube emulation\n");
  fprintf(stream, "  -v                -- print version number then exit\n");
  fprintf(stream, "  -w                -- write memory to file run6502.out on exit\n");
  fprintf(stream, "  -X addr           -- terminate emulation if PC reaches addr\n");
  fprintf(stream, "  -x                -- exit without further ado\n");
  fprintf(stream, "  image             -- '-l 8000 image' in available ROM slot\n");
  fprintf(stream, "\n");
  fprintf(stream, "'last' can be an address (non-inclusive) or '+size' (in bytes)\n");
  exit(status);
}


static int doHelp(int argc, char **argv, M6502 *mpu)
{
  usage(0);
  return 0;
}


static int doVersion(int argc, char **argv, M6502 *mpu)
{
  puts(VERSION);
  exit(0);
  return 0;
}


static int save(M6502 *mpu, word address, unsigned length, const char *path)
{
  FILE *file= 0;
  int   count= 0;
  if (!(file= fopen(path, "w")))
    return 0;
  while ((count= fwrite(mpu->memory + address, 1, length, file)))
    {
      address += count;
      length -= count;
    }
  fclose(file);
  return 1;
}


static int load(M6502 *mpu, word address, const char *path)
{
  FILE  *file= 0;
  int    count= 0;
  size_t max= 0x10000 - address;
  if (!(file= fopen(path, "r")))
    return 0;
  while ((count= fread(mpu->memory + address, 1, max, file)) > 0)
    {
      address += count;
      max -= count;
    }
  fclose(file);
  return 1;
}


static void writeMemory(void)
{
  if (!exit_write_mpu)
    return;
  if (!save(exit_write_mpu, 0, 0x10000, "run6502.out")) 
    pfail("run6502.out");
}


/* TODO: Although -s is a startup option, we could make -w defer the action of -s,
 * respecting its parameters when the time comes to save, instead of using a
 * hard-coded address range and filename.
 */
static int doExitWrite(int argc, char **argv, M6502 *mpu)
{
  exit_write= 1;
  exit_write_mpu= mpu;
  atexit(writeMemory);
  return 1;
}


static unsigned long htol(char *hex)
{
  char *end;
  unsigned long l= strtol(hex, &end, 16);
  if (*end) fail("bad hex number: %s", hex);
  return l;
}


static int loadInterpreter(M6502 *mpu, word start, const char *path)
{
  FILE   *file= 0;
  int     count= 0;
  byte   *memory= mpu->memory + start;
  size_t  max= 0x10000 - start;
  int     c= 0;

  if ((!(file= fopen(path, "r"))) || ('#' != fgetc(file)) || ('!' != fgetc(file)))
    return 0;
  while ((c= fgetc(file)) >= ' ')
    ;
  while ((count= fread(memory, 1, max, file)) > 0)
    {
      memory += count;
      max -= count;
    }
  fclose(file);
  return 1;
}


static int doLoadInterpreter(int argc, char **argv, M6502 *mpu)
{
  if (argc < 3) usage(1);
  if (!loadInterpreter(mpu, htol(argv[1]), argv[2])) pfail(argv[2]);
  return 2;
}


static int doLoad(int argc, char **argv, M6502 *mpu)	/* -l addr file */
{
  if (argc < 3) usage(1);
  if (!load(mpu, htol(argv[1]), argv[2])) pfail(argv[2]);
  return 2;
}


static int doSave(int argc, char **argv, M6502 *mpu)	/* -l addr size file */
{
  if (argc < 4) usage(1);
  if (!save(mpu, htol(argv[1]), htol(argv[2]), argv[3])) pfail(argv[3]);
  return 3;
}


#define doVEC(VEC)					\
  static int do##VEC(int argc, char **argv, M6502 *mpu)	\
    {							\
      unsigned addr= 0;					\
      if (argc < 2) usage(1);				\
      addr= htol(argv[1]);				\
      M6502_setVector(mpu, VEC, addr);			\
      return 1;						\
    }

doVEC(IRQ);
doVEC(NMI);
doVEC(RST);

#undef doVEC


static int gTrap(M6502 *mpu, word addr, byte data)	{ mpu->registers->a= getchar();  rts; }
static int pTrap(M6502 *mpu, word addr, byte data)	{ putchar(mpu->registers->a);  rts; }

static int doGtrap(int argc, char **argv, M6502 *mpu)
{
  unsigned addr;
  if (argc < 2) usage(1);
  addr= htol(argv[1]);
  M6502_setCallback(mpu, call, addr, gTrap);
  return 1;
}

static int doPtrap(int argc, char **argv, M6502 *mpu)
{
  unsigned addr;
  if (argc < 2) usage(1);
  addr= htol(argv[1]);
  M6502_setCallback(mpu, call, addr, pTrap);
  return 1;
}


static int mTrapRead(M6502 *mpu, word addr, byte data)	{ return getchar(); }
static int mTrapWrite(M6502 *mpu, word addr, byte data)	{ return putchar(data); }

static int doMtrap(int argc, char **argv, M6502 *mpu)
{
  unsigned addr= 0;
  if (argc < 2) usage(1);
  addr= htol(argv[1]);
  M6502_setCallback(mpu, read,  addr, mTrapRead);
  M6502_setCallback(mpu, write, addr, mTrapWrite);
  return 1;
}


static int xTrap(M6502 *mpu, word addr, byte data)	{ exit(0);  return 0; }

static int doXtrap(int argc, char **argv, M6502 *mpu)
{
  unsigned addr= 0;
  if (argc < 2) usage(1);
  addr= htol(argv[1]);
  M6502_setCallback(mpu, call, addr, xTrap);
  return 1;
}


static int doTubeCommand(int argc, char **argv, M6502 *mpu)
{
  if (argc < 2) usage(1);
  tube_command = argv[1]; 
  return 1;
}


static int doDisassemble(int argc, char **argv, M6502 *mpu)
{
  unsigned addr= 0, last= 0;
  if (argc < 3) usage(1);
  addr= htol(argv[1]);
  last= ('+' == *argv[2]) ? addr + htol(1 + argv[2]) : htol(argv[2]);
  while (addr < last)
    {
      char insn[64];
      int  i= 0, size= M6502_disassemble(mpu, addr, insn);
      printf("%04X ", addr);
      while (i++ < size)  printf("%02X", mpu->memory[addr + i - 1]);
      while (i++ < 4)     printf("  ");
      putchar(' ');
      i= 0;
      while (i++ < size)  putchar(isgraph(mpu->memory[addr + i - 1]) ? mpu->memory[addr + i - 1] : ' ');
      while (i++ < 4)     putchar(' ');
      printf(" %s\n", insn);
      addr += size;
    }
  return 2;
}


int main(int argc, char **argv)
{
  M6502 *mpu= M6502_new(0, 0, 0);
  int bTraps= 0;
  int tTraps= 0;

  program= argv[0];

  if ((2 == argc) && ('-' != *argv[1]))
    {
      if ((!loadInterpreter(mpu, 0, argv[1])) && (!load(mpu, 0, argv[1])))
	pfail(argv[1]);
      doBtraps(0, 0, mpu);
    }
  else
    while (++argv, --argc > 0)
      {
	int n= 0;
	if      (!strcmp(*argv, "-B"))  bTraps= 1;
        else if (!strcmp(*argv, "-c"))  n= doTubeCommand(argc, argv, mpu);
	else if (!strcmp(*argv, "-d"))	n= doDisassemble(argc, argv, mpu);
	else if (!strcmp(*argv, "-G"))	n= doGtrap(argc, argv, mpu);
	else if (!strcmp(*argv, "-h"))	n= doHelp(argc, argv, mpu);
	else if (!strcmp(*argv, "-i"))	n= doLoadInterpreter(argc, argv, mpu);
	else if (!strcmp(*argv, "-I"))	n= doIRQ(argc, argv, mpu);
	else if (!strcmp(*argv, "-l"))	n= doLoad(argc, argv, mpu);
	else if (!strcmp(*argv, "-M"))	n= doMtrap(argc, argv, mpu);
	else if (!strcmp(*argv, "-N"))	n= doNMI(argc, argv, mpu);
	else if (!strcmp(*argv, "-P"))	n= doPtrap(argc, argv, mpu);
	else if (!strcmp(*argv, "-R"))	n= doRST(argc, argv, mpu);
	else if (!strcmp(*argv, "-s"))	n= doSave(argc, argv, mpu);
	else if (!strcmp(*argv, "-T"))  tTraps= 1;
	else if (!strcmp(*argv, "-v"))	n= doVersion(argc, argv, mpu);
	else if (!strcmp(*argv, "-w"))  n= doExitWrite(argc, argv, mpu);
	else if (!strcmp(*argv, "-X"))	n= doXtrap(argc, argv, mpu);
	else if (!strcmp(*argv, "-x"))	exit(0);
	else if ('-' == **argv)		usage(1);
	else
	  {
	    /* doBtraps() left 0x8000+0x4000 in bank 0, so load */
	    /* additional images starting at 15 and work down */
	    static int bankSel= 0x0F;
	    if (!bTraps)			usage(1);
	    if (bankSel < 0)			fail("too many images");
	    if (!load(mpu, 0x8000, argv[0]))	pfail(argv[0]);
	    memcpy(bank[bankSel--],
		   0x8000 + mpu->memory,
		   0x4000);
	    n= 1;
	  }
	argc -= n;
	argv += n;
      }

  if (bTraps && tTraps)
    fail("-B and -T are incompatible");

  if (tube_command && !tTraps)
    fail("-c is only valid with -T");

  if (bTraps)
    doBtraps(0, 0, mpu);
  else if (tTraps)
    doTtraps(0, 0, mpu);

  M6502_reset(mpu);
  M6502_run(mpu);

  if (exit_write)
    writeMemory();
  exit_write_mpu= 0; M6502_delete(mpu);

  return 0;
}
