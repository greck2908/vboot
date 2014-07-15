/*
 * Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "futility.h"
#include "gbb_header.h"

static void help_and_quit(const char *prog) {
  fprintf(stderr, "\n"
	  "Usage: %s [-g|-s|-c] [OPTIONS] bios_file [output_file]\n"
	  "\n"
	  "GET MODE:\n"
	  "-g, --get   (default)\tGet (read) from bios_file, "
	  "with following options:\n"
	  "     --hwid          \tReport hardware id (default).\n"
	  "     --flags         \tReport header flags.\n"
	  " -k, --rootkey=FILE  \tFile name to export Root Key.\n"
	  " -b, --bmpfv=FILE    \tFile name to export Bitmap FV.\n"
	  " -r  --recoverykey=FILE\tFile name to export Recovery Key.\n"
	  "\n"
	  "SET MODE:\n"
	  "-s, --set            \tSet (write) to bios_file, "
	  "with following options:\n"
	  " -o, --output=FILE   \tNew file name for ouptput.\n"
	  "     --hwid=HWID     \tThe new hardware id to be changed.\n"
	  "     --flags=FLAGS   \tThe new (numeric) flags value.\n"
	  " -k, --rootkey=FILE  \tFile name of new Root Key.\n"
	  " -b, --bmpfv=FILE    \tFile name of new Bitmap FV.\n"
	  " -r  --recoverykey=FILE\tFile name of new Recovery Key.\n"
	  "\n"
	  "CREATE MODE:\n"
	  "-c, --create=hwid_size,rootkey_size,bmpfv_size,recoverykey_size\n"
	  "                     \tCreate a GBB blob by given size list.\n"
	  "SAMPLE:\n"
	  "  %s -g bios.bin\n"
	  "  %s --set --hwid='New Model' -k key.bin bios.bin newbios.bin\n"
	  "  %s -c 0x100,0x1000,0x03DE80,0x1000 gbb.blob\n\n",
	  prog, prog, prog, prog);
  exit(1);
}

/* Command line options */
static struct option long_opts[] = {
  /* name    hasarg *flag val */
  {"get",         0,   NULL, 'g' },
  {"set",         0,   NULL, 's' },
  {"create",      1,   NULL, 'c' },
  {"output",      1,   NULL, 'o' },
  {"rootkey",     1,   NULL, 'k' },
  {"bmpfv",       1,   NULL, 'b' },
  {"recoverykey", 1,   NULL, 'R' },
  {"hwid",        2,   NULL, 'i' },
  {"flags",       2,   NULL, 'L' },
  { NULL, 0, NULL, 0 },
};
static char *short_opts = ":gsc:o:k:b:R:r:h:i:L:f:";

static int errorcnt;

static int ValidGBB(GoogleBinaryBlockHeader *gbb, size_t maxlen)
{
  uint32_t i;
  char *s;

  if (gbb->major_version != GBB_MAJOR_VER)
    goto bad;
  if (gbb->header_size != GBB_HEADER_SIZE || gbb->header_size > maxlen)
    goto bad;
  if (gbb->hwid_offset < GBB_HEADER_SIZE)
    goto bad;
  if (gbb->hwid_offset + gbb->hwid_size > maxlen)
    goto bad;
  if (gbb->hwid_size) {
    /* Make sure the HWID is null-terminated (assumes ASCII, not unicode). */
    s = (char *)((char *)gbb + gbb->hwid_offset);
    for (i = 0; i < gbb->hwid_size; i++)
      if (*s++ == '\0')
	break;
    if (i >= gbb->hwid_size)
      goto bad;
  }
  if (gbb->rootkey_offset < GBB_HEADER_SIZE)
    goto bad;
  if (gbb->rootkey_offset + gbb->rootkey_size > maxlen)
    goto bad;
  if (gbb->bmpfv_offset < GBB_HEADER_SIZE)
    goto bad;
  if (gbb->bmpfv_offset + gbb->bmpfv_size > maxlen)
    goto bad;
  if (gbb->recovery_key_offset < GBB_HEADER_SIZE)
    goto bad;
  if (gbb->recovery_key_offset + gbb->recovery_key_size > maxlen)
    goto bad;

  return 1;

bad:
  errorcnt++;
  return 0;
}

#define GBB_SEARCH_STRIDE 4
GoogleBinaryBlockHeader *FindGbbHeader(uint8_t *ptr, size_t size)
{
  size_t i;
  GoogleBinaryBlockHeader *tmp, *gbb_header = NULL;
  int count = 0;

  for (i = 0;
       i <= size - GBB_SEARCH_STRIDE;
       i += GBB_SEARCH_STRIDE) {
    if (0 != memcmp(ptr + i, GBB_SIGNATURE, GBB_SIGNATURE_SIZE))
      continue;

    /* Found something. See if it's any good. */
    tmp = (GoogleBinaryBlockHeader *)(ptr + i);
    if (ValidGBB(tmp, size - i))
      if (!count++)
	gbb_header = tmp;
  }

  switch (count) {
  case 0:
    errorcnt++;
    return NULL;
  case 1:
    return gbb_header;
  default:
    fprintf(stderr, "ERROR: multiple GBB headers found\n");
    errorcnt++;
    return NULL;
  }
}

static uint8_t *create_gbb(const char *desc, off_t *sizeptr)
{
  char *str, *sizes, *param, *e = NULL;
  size_t size = GBB_HEADER_SIZE;
  int i = 0;
  /* Danger Will Robinson! four entries ==> four paramater blocks */
  uint32_t val[] = {0, 0, 0, 0};
  uint8_t *buf;
  GoogleBinaryBlockHeader *gbb;

  sizes = strdup(desc);
  if (!sizes) {
    errorcnt++;
    fprintf(stderr, "ERROR: strdup() failed: %s\n", strerror(errno));
    return NULL;
  }

  for (str = sizes; (param = strtok(str, ", ")) != NULL; str = NULL) {
    val[i] = (uint32_t)strtoul(param, &e, 0);
    if (e && *e) {
      errorcnt++;
      fprintf(stderr, "ERROR: invalid creation parameter: \"%s\"\n", param);
      free(sizes);
      return NULL;
    }
    size += val[i++];
    if (i > ARRAY_SIZE(val))
      break;
  }

  buf = (uint8_t *)calloc(1, size);
  if (!buf) {
    errorcnt++;
    fprintf(stderr, "ERROR: can't malloc %zu bytes: %s\n",
	    size, strerror(errno));
    free(sizes);
    return NULL;
  } else if (sizeptr) {
    *sizeptr = size;
  }

  gbb = (GoogleBinaryBlockHeader *)buf;
  memcpy(gbb->signature, GBB_SIGNATURE, GBB_SIGNATURE_SIZE);
  gbb->major_version = GBB_MAJOR_VER;
  gbb->minor_version = GBB_MINOR_VER;
  gbb->header_size = GBB_HEADER_SIZE;
  gbb->flags = 0;

  i = GBB_HEADER_SIZE;
  gbb->hwid_offset = i;
  gbb->hwid_size = val[0];
  i += val[0];

  gbb->rootkey_offset = i;
  gbb->rootkey_size = val[1];
  i += val[1];

  gbb->bmpfv_offset = i;
  gbb->bmpfv_size = val[2];
  i += val[2];

  gbb->recovery_key_offset = i;
  gbb->recovery_key_size = val[3];
  i += val[1];

  free(sizes);
  return buf;
}

uint8_t *read_entire_file(const char *filename, off_t *sizeptr)
{
  FILE *fp = NULL;
  uint8_t *buf = NULL;
  struct stat sb;

  fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "ERROR: Unable to open %s for reading: %s\n",
	    filename, strerror(errno));
    goto fail;
  }

  if (0 != fstat(fileno(fp), &sb)) {
    fprintf(stderr, "ERROR: can't fstat %s: %s\n",
	    filename, strerror(errno));
    goto fail;
  }
  if (sizeptr)
    *sizeptr = sb.st_size;

  buf = (uint8_t *)malloc(sb.st_size);
  if (!buf) {
    fprintf(stderr, "ERROR: can't malloc %" PRIi64 " bytes: %s\n",
	    sb.st_size, strerror(errno));
    goto fail;
  }

  if (1 != fread(buf, sb.st_size, 1, fp)) {
    fprintf(stderr, "ERROR: Unable to read from %s: %s\n",
	    filename, strerror(errno));
    goto fail;
  }

  if (fp && 0 != fclose(fp)) {
    fprintf(stderr, "ERROR: Unable to close %s: %s\n",
	    filename, strerror(errno));
    goto fail;
  }

  return buf;

fail:
  errorcnt++;

  if (buf)
    free(buf);

  if (fp && 0 != fclose(fp))
    fprintf(stderr, "ERROR: Unable to close %s: %s\n",
	    filename, strerror(errno));
  return NULL;
}

static int write_to_file(const char *msg, const char *filename,
			 uint8_t *start, size_t size)
{
  FILE *fp;
  int r = 0;

  fp = fopen(filename, "wb");
  if (!fp) {
    fprintf(stderr, "ERROR: Unable to open %s for writing: %s\n",
	    filename, strerror(errno));
    errorcnt++;
    return errno;
  }

  /* Don't write zero bytes */
  if (size && 1 != fwrite(start, size, 1, fp)) {
    fprintf(stderr, "ERROR: Unable to write to %s: %s\n",
	    filename, strerror(errno));
    errorcnt++;
    r = errno;
  }

  if (0 != fclose(fp)) {
    fprintf(stderr, "ERROR: Unable to close %s: %s\n",
	    filename, strerror(errno));
    errorcnt++;
    if (!r)
      r = errno;
  }

  if (!r && msg)
    printf("%s %s\n", msg, filename);

  return r;
}

static int read_from_file(const char *msg, const char *filename,
			  uint8_t *start, uint32_t size)
{
  FILE *fp;
  struct stat sb;
  size_t count;
  int r = 0;

  fp = fopen(filename, "rb");
  if (!fp) {
    fprintf(stderr, "ERROR: Unable to open %s for reading: %s\n",
	    filename, strerror(errno));
    errorcnt++;
    return errno;
  }

  if (0 != fstat(fileno(fp), &sb)) {
    fprintf(stderr, "ERROR: can't fstat %s: %s\n",
	    filename, strerror(errno));
    errorcnt++;
    r = errno;
    goto done_close;
  }

  if (sb.st_size > size) {
    fprintf(stderr, "ERROR: file %s exceeds capacity (%" PRIu32 ")\n",
	    filename, size);
    errorcnt++;
    r = errno;
    goto done_close;
  }

  /* It's okay if we read less than size. That's just the max. */
  count = fread(start, 1, size, fp);
  if (ferror(fp)) {
    fprintf(stderr, "ERROR: Read %zu/%" PRIi64 " bytes from %s: %s\n",
	    count, sb.st_size, filename, strerror(errno));
    errorcnt++;
    r = errno;
  }

done_close:
  if (0 != fclose(fp)) {
    fprintf(stderr, "ERROR: Unable to close %s: %s\n",
	    filename, strerror(errno));
    errorcnt++;
    if (!r)
      r = errno;
  }

  if (!r && msg)
    printf(" - import %s from %s: success\n", msg, filename);

  return r;
}

static int do_gbb_utility(int argc, char *argv[])
{
  enum do_what_now {DO_GET, DO_SET, DO_CREATE} mode = DO_GET;
  char *infile = NULL;
  char *outfile = NULL;
  char *opt_create = NULL;
  char *opt_rootkey = NULL;
  char *opt_bmpfv = NULL;
  char *opt_recoverykey = NULL;
  char *opt_hwid = NULL;
  char *opt_flags = NULL;
  int sel_hwid = 0;
  int sel_flags = 0;
  uint8_t *inbuf = NULL;
  off_t filesize;
  uint8_t *outbuf = NULL;
  GoogleBinaryBlockHeader *gbb;
  uint8_t *gbb_base;
  int i;

  opterr = 0;					/* quiet, you */
  while ((i = getopt_long(argc, argv, short_opts, long_opts, 0)) != -1) {
    switch (i) {
    case 'g':
      mode = DO_GET;
      break;
    case 's':
      mode = DO_SET;
      break;
    case 'c':
      mode = DO_CREATE;
      opt_create = optarg;
      break;
    case 'o':
      outfile = optarg;
      break;
    case 'k':
      opt_rootkey = optarg;
      break;
    case 'b':
      opt_bmpfv = optarg;
      break;
    case 'R': case 'r':
      opt_recoverykey = optarg;
      break;
    case 'i': case 'h':
      /* --hwid is optional: might be null, which could be okay */
      opt_hwid = optarg;
      sel_hwid = 1;
      break;
    case 'L': case 'f':
      /* --flags is optional: might be null, which could be okay */
      opt_flags = optarg;
      sel_flags = 1;
      break;
    case '?':
      errorcnt++;
      if (optopt)
	fprintf(stderr, "ERROR: unrecognized option: -%c\n", optopt);
      else if (argv[optind - 1] )
	fprintf(stderr, "ERROR: unrecognized option (possibly \"%s\")\n",
		argv[optind - 1]);
      else
	fprintf(stderr, "ERROR: unrecognized option\n");
      break;
    case ':':
      errorcnt++;
      if (argv[optind - 1])
	fprintf(stderr, "ERROR: missing argument to -%c (%s)\n",
		optopt, argv[optind - 1]);
      else
	fprintf(stderr, "ERROR: missing argument to -%c\n", optopt);
      break;
    default:
      errorcnt++;
      fprintf(stderr, "ERROR: unexpected error while parsing options\n");
    }
  }

  /* Problems? */
  if (errorcnt)
    help_and_quit(argv[0]);

  /* Now try to do something */
  switch (mode) {
  case DO_GET:
    if (argc - optind < 1) {
      fprintf(stderr, "\nERROR: missing input filename\n");
      help_and_quit(argv[0]);
    } else {
      infile = argv[optind++];
    }

    /* With no args, show the HWID */
    if (!opt_rootkey && !opt_bmpfv && !opt_recoverykey && !sel_flags)
      sel_hwid = 1;

    inbuf = read_entire_file(infile, &filesize);
    if (!inbuf)
      break;

    gbb = FindGbbHeader(inbuf, filesize);
    if (!gbb) {
      fprintf(stderr, "ERROR: No GBB found in %s\n", infile);
      break;
    }
    gbb_base = (uint8_t *)gbb;

    /* Get the stuff */
    if (sel_hwid)
	printf("hardware_id: %s\n",
	       gbb->hwid_size ? (char *)(gbb_base + gbb->hwid_offset) : "");
    if (sel_flags)
      printf("flags: 0x%08x\n", gbb->flags);
    if (opt_rootkey)
      write_to_file(" - exported root_key to file:", opt_rootkey,
		    gbb_base + gbb->rootkey_offset,
		    gbb->rootkey_size);
    if (opt_bmpfv)
      write_to_file(" - exported bmp_fv to file:", opt_bmpfv,
		    gbb_base + gbb->bmpfv_offset,
		    gbb->bmpfv_size);
    if (opt_recoverykey)
      write_to_file(" - exported recovery_key to file:", opt_recoverykey,
		    gbb_base + gbb->recovery_key_offset,
		    gbb->recovery_key_size);
    break;

  case DO_SET:
    if (argc - optind < 1) {
      fprintf(stderr, "\nERROR: missing input filename\n");
      help_and_quit(argv[0]);
    }
    infile = argv[optind++];
    if (!outfile)
      outfile = (argc - optind < 1) ? infile : argv[optind++];

    if (sel_hwid && !opt_hwid) {
      fprintf(stderr, "\nERROR: missing new HWID value\n");
      help_and_quit(argv[0]);
    }
    if (sel_flags && (!opt_flags || !*opt_flags)) {
      fprintf(stderr, "\nERROR: missing new flags value\n");
      help_and_quit(argv[0]);
    }

    /* With no args, we'll either copy it unchanged or do nothing */
    inbuf = read_entire_file(infile, &filesize);
    if (!inbuf)
      break;

    gbb = FindGbbHeader(inbuf, filesize);
    if (!gbb) {
      fprintf(stderr, "ERROR: No GBB found in %s\n", infile);
      break;
    }
    gbb_base = (uint8_t *)gbb;

    outbuf = (uint8_t *)malloc(filesize);
    if (!outbuf) {
      errorcnt++;
      fprintf(stderr, "ERROR: can't malloc %" PRIi64 " bytes: %s\n",
	      filesize, strerror(errno));
      break;
    }

    /* Switch pointers to outbuf */
    memcpy(outbuf, inbuf, filesize);
    gbb = FindGbbHeader(outbuf, filesize);
    if (!gbb) {
      fprintf(stderr, "INTERNAL ERROR: No GBB found in outbuf\n");
      exit(1);
    }
    gbb_base = (uint8_t *)gbb;

    if (opt_hwid) {
      if (strlen(opt_hwid) + 1 > gbb->hwid_size) {
	fprintf(stderr,
		"ERROR: null-terminated HWID exceeds capacity (%d)\n",
		gbb->hwid_size);
	errorcnt++;
      } else {
	strcpy((char *)(gbb_base + gbb->hwid_offset), opt_hwid);
      }
    }

    if (opt_flags) {
      char *e = NULL;
      uint32_t val;
      val = (uint32_t)strtoul(opt_flags, &e, 0);
      if (e && *e) {
	fprintf(stderr, "ERROR: invalid flags value: %s\n", opt_flags);
	errorcnt++;
      } else {
	gbb->flags = val;
      }
    }

    if (opt_rootkey)
      read_from_file("root_key", opt_rootkey,
		    gbb_base + gbb->rootkey_offset,
		    gbb->rootkey_size);
    if (opt_bmpfv)
      read_from_file("bmp_fv", opt_bmpfv,
		    gbb_base + gbb->bmpfv_offset,
		    gbb->bmpfv_size);
    if (opt_recoverykey)
      read_from_file("recovery_key", opt_recoverykey,
		    gbb_base + gbb->recovery_key_offset,
		    gbb->recovery_key_size);

    /* Write it out if there are no problems. */
    if (!errorcnt)
      write_to_file("successfully saved new image to:",
		    outfile, outbuf, filesize);

    break;

    case DO_CREATE:
      if (!outfile) {
	if (argc - optind < 1) {
	  fprintf(stderr, "\nERROR: missing output filename\n");
	  help_and_quit(argv[0]);
	}
	outfile = argv[optind++];
      }
      /* Parse the creation args */
      outbuf = create_gbb(opt_create, &filesize);
      if (!outbuf) {
	fprintf(stderr,
		"\nERROR: unable to parse creation spec (%s)\n", opt_create);
	help_and_quit(argv[0]);
      }
      if (!errorcnt)
	write_to_file("successfully created new GBB to:",
		      outfile, outbuf, filesize);
      break;
  }


  if (inbuf)
    free(inbuf);
  if (outbuf)
    free(outbuf);
  return !!errorcnt;
}

DECLARE_FUTIL_COMMAND(gbb_utility, do_gbb_utility,
		      "Utility to manage Google Binary Block (GBB)");