#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

const char *driverName = "";
const char *fwName = "nvstusb.fw";
const char *dumpName = "nvstusb.mem";

static FILE *
openDriver(
  const char *fname
) {
  FILE *f = fopen(fname, "rb");
  if (0 != f) {
    driverName = fname;
    fprintf(stderr, "opened '%s'\n", fname);
  } else {
    fprintf(stderr, "could not open '%s': %s\n", fname, strerror(errno));
  }
  return f;
}

void
seekFile(
  FILE *f,
  const char *fname,
  unsigned long pos
) {
  int res = fseek(f, pos, SEEK_SET);
  if (res != 0) {
    fprintf(stderr, "%s: Could not seek to position %lu!\n", fname, pos);
    fclose(f);
    exit(1);
  }
}

void 
readFile(
  FILE *f,
  const char *fname,
  void *dest,
  size_t size
) {
  if (fread(dest, size, 1, f) != 1) {
    fprintf(stderr, "%s: Could not read %lu bytes!\n", fname, size);
  }
} 

void 
readFileAt(
  FILE *f,
  const char *fname,
  size_t offset,
  void *dest,
  size_t size
) {
  seekFile(f, fname, offset);
  readFile(f, fname, dest, size);
} 

unsigned short
readFileWORD(
  FILE *f,
  const char *fname,
  size_t offset
) {
  unsigned char buf[2];
  unsigned short v = 0;
  readFileAt(f, fname, offset, buf, 2);
  return buf[0] | (buf[1]<<8);
}

unsigned long
readFileDWORD(
  FILE *f,
  const char *fname,
  size_t offset
) {
  unsigned char buf[4];
  unsigned short v = 0;
  readFileAt(f, fname, offset, buf, 4);
  return buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
}

void 
writeFile(
  FILE *f,
  const char *fname,
  void *data,
  size_t size
) {
  if (fwrite(data, size, 1, f) != 1) {
    fprintf(stderr, "%s: Could not write %lu bytes!\n", fname, size);
  }
} 

size_t 
findDataSection(
  FILE *f,
  size_t *size
) {
  char buf[1024];

  /* check for MZ header */
  const char headerMZ[2] = { 'M', 'Z' };
  readFileAt(f, driverName, 0, buf, 2);
  if (memcmp(buf, headerMZ, 2) != 0) {
    fprintf(stderr, "%s: Not a driver file!\n", driverName);
    return 0;
  }

  /* find PE header */
  size_t peHeaderOffset = readFileDWORD(f, driverName, 0x0000003C);

  /* check for PE header */
  const char headerPE[4] = { 'P', 'E', 0, 0 };
  readFileAt(f, driverName, peHeaderOffset, buf, 4);
  if (memcmp(buf, headerPE, 4) != 0) {
    fprintf(stderr, "%s: Not a driver file!\n", driverName);
    return 0;
  }

  /* read section information */
  peHeaderOffset += 4;

  unsigned short sectionCount = 
    readFileWORD(f, driverName, peHeaderOffset + 2);

  fprintf(stderr, "found %hu sections\n", sectionCount);

  unsigned short optionalHeaderSize = 
    readFileWORD(f, driverName, peHeaderOffset + 16);

  fprintf(stderr, "%hu bytes of optional PE header\n", optionalHeaderSize);

  size_t sectionOffset = peHeaderOffset + 20 + optionalHeaderSize;
  fprintf(stderr, "first section header at %08x\n", sectionOffset);

  unsigned short i;
  for (i=0; i<sectionCount; i++) {
    size_t curSectionOffset = sectionOffset + 40 * i;
    char sectionName[9];
    memset(sectionName, 0, 9);

    readFileAt(f, driverName, curSectionOffset, sectionName, 8);
    size_t curSectionSize = 
      readFileDWORD(f, driverName, curSectionOffset + 16);
    size_t curSectionAddress = 
      readFileDWORD(f, driverName, curSectionOffset + 20);

    if (0 == strcmp(sectionName, ".data")) {
      fprintf(stderr, "found .data section at %08x\n", curSectionAddress);
      *size = curSectionSize;
      return curSectionAddress;
    }
  }
  fprintf(stderr, "%s: could not find data section!\n", driverName);
  exit(1);
  return 0;
}

size_t 
findFirmware(
  FILE *f
) {
  size_t dataSectionSize   = 0;
  size_t dataSectionOffset = findDataSection(f, &dataSectionSize);

  unsigned char signature[8] = {
    0xC2, 0x55, 0x09, 0x07, 0x00, 0x00, 0x00, 0x00
  };

  unsigned char buf[8];
  readFileAt(f, driverName, dataSectionOffset + 0x30, buf, 8);
  if (0 == memcmp(buf, signature, 8)) {
    size_t fwOffset = dataSectionOffset + 0x38;
    fprintf(stderr, "probably found firmware at %08x\n", fwOffset);
    return fwOffset;
  }

  exit(1);
  return 0;
}

int main(int argc, char **argv) {
  /* open driver file */
  FILE *f = 0;
  if (argc > 1) {
    f = openDriver(argv[1]);
  } else {
    f = openDriver("nvstusb.sys");
#ifdef _WIN32
    if (!f) f = openDriver("c:\\Windows\\System32\\drivers\\nvstusb.sys");
#endif  
  }
  if (!f) {
    exit(1);
  }

  /* seek to beginning of firmware */
  unsigned long offset = findFirmware(f);
  if (0 == offset) return -1;

  seekFile(f, driverName, offset);
  
  /* open firmware file */
  FILE *of = fopen(fwName, "wb");
  if (!of) { 
    fprintf(stderr, "%s: could not open file: %s\n", fwName, strerror(errno)); 
    exit(1);
  }
  fprintf(stderr, "opened '%s' for output\n", fwName);

  /* add command to be automatically send before sending the firmware */
  unsigned char cfg1[5] = { 
    0x00, 0x01, /* one byte of data */
    0xE6, 0x00, /* write to 0xE600 = CPUCS */
    0x01        /* bit 0 (8051RES) set == put controller into reset. */
  };
  writeFile(of, fwName, cfg1, 5);

  /* initialize firmware memory dump */
  char mem[0x2000];
  memset(mem, 0, 0x2000);

  int block = 0;
  do {
    /* read block header */
    unsigned char lenPos[4];
    readFile(f, driverName, lenPos, 4);

    /* stop after last block */
    if (lenPos[0] & 0x80) break;

    unsigned short length  = (lenPos[0] << 8) | lenPos[1];
    unsigned short address = (lenPos[2] << 8) | lenPos[3];

    fprintf(stderr, "block %3u: %10u bytes at 0x%04x\n", block, length, address);

    /* read block */
    char buf[1024];
    readFile(f, driverName, buf, length);
    writeFile(of, fwName, lenPos, 4);
    writeFile(of, fwName, buf, length);

    /* copy block to memory dump */
    memcpy(mem+address, buf, length);

    block++;
  } while(1);

  /* command "leave fw setup mode" */
  unsigned char cfg2[5] = { 
    0x00, 0x01, /* one byte of data */
    0xE6, 0x00, /* write to 0xE600 = CPUCS */
    0x00        /* bit 0 (8051RES) clear == let controller run. */
  };
  if (fwrite(cfg2, 5, 1, of) != 1) { perror(fwName); return errno; }
  
  /* clean up */
  fclose(of);
  fclose(f);

  /* write memory dump */

  FILE *ff = fopen(dumpName, "wb");
  if (ff) {
    fprintf(stderr, "writing firmware memory dump to '%s'\n", dumpName);
    writeFile(ff, dumpName, mem, 0x2000);
    fclose(ff);
  } else {
    fprintf(stderr, "could not open '%s' for writing memory dump\n", dumpName);
  }

  fprintf(stderr, "done extracting firmware...\n");
  return 0;
}
