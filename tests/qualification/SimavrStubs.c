/* simavr's loader of firmware files, which the differential tests do not use: they write the flash directly. sim_hex.c
   stays compiled for read_hex_string(), needed by the GDB stub; its file loader refers to the ELF loader, left out
   because it needs libelf. */
#include "sim_elf.h"

int elf_read_firmware(const char * file, elf_firmware_t * firmware) {
	(void)file;
	(void)firmware;
	return -1;
}

void sim_default_mcu(elf_firmware_t * firmware, const char * filename) {
	(void)firmware;
	(void)filename;
}
