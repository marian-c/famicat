#include "famicom/memory.h"
#include "famicom/cart.h"
#include "famicom/apu.h"
#include "famicom/ppu.h"

Memory memory;
extern Cart cart;
extern Mapper nrom, mmc1, unrom, cnrom, mmc3, mmc5, mmc2, mmc4;

void Memory_Init() {
	if (memory.ram != NULL) {
		free(memory.ram);
	}

	if (memory.exprom != NULL) {
		free(memory.exprom);
	}

	if (memory.nametable0 != NULL) {
		free(memory.nametable0);
	}

	if (memory.nametable1 != NULL) {
		free(memory.nametable1);
	}

	if (memory.nametable2 != NULL) {
		free(memory.nametable2);
	}

	if (memory.nametable3 != NULL) {
		free(memory.nametable3);
	}

	if (memory.paletteram != NULL) {
		free(memory.paletteram);
	}

	memory.ram = (BYTE*)calloc(0x800, sizeof(BYTE));
	memory.exprom = (BYTE*)calloc(0x1FDF, sizeof(BYTE));
	memory.paletteram = (BYTE*)calloc(0x20, sizeof(BYTE));

	// Set up nametable mirroring
	switch (cart.vramarr) {
		case H_MIRROR:
			memory.nametable0 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable1 = memory.nametable0;
			memory.nametable2 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable3 = memory.nametable2;
			break;

		case V_MIRROR:
			memory.nametable0 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable1 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable2 = memory.nametable0;
			memory.nametable3 = memory.nametable1;
			break;

		case Q_SCREEN:
			memory.nametable0 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable1 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable2 = (BYTE*)calloc(0x400, sizeof(BYTE));
			memory.nametable3 = (BYTE*)calloc(0x400, sizeof(BYTE));
			break;
	}
}

void Memory_SetMapper(int mapper) {
	switch (mapper) {
		case 0: memory.mapper = nrom; break;
		case 1: memory.mapper = mmc1; break;
		default: memory.mapper = nrom; break;
	}

	memory.mapper.init();
}

BYTE Memory_ReadByte(int map, WORD addr) {
	// CPU memory map
	if (map == MAP_CPU) {
		// Working RAM + mirrors
		if (addr < 0x2000) {
			return memory.ram[addr % 0x800];
		}

		// PPU control registers + mirrors
		else if (addr >= 0x2000 && addr < 0x4000) {
			switch (addr) {
				case 0x2000: // Controller
				case 0x2001: // Mask
				case 0x2003: // OAM address
				case 0x2005: // Scroll
				case 0x2006: // Address
					break;
				case 0x2002: // Status
					return PPU_ReadStatus();
					break;
				case 0x2004: // OAM data
					return PPU_ReadOAMData();
					break;
				case 0x2007: // Data
					return PPU_ReadData();
					break;
				default:
					return Memory_InvalidRead(MAP_CPU, addr);
			}
		}

		// OAM DMA (write only)
		else if (addr == 0x4014) {
			return Memory_InvalidRead(MAP_CPU, addr);
		}

		// APU registers
		else if (addr >= 0x4000 && addr < 0x4020) {
			// return APU_ReadRegister(addr);
			return 0;
		}

		// Cartridge expansion ROM
		else if (addr >= 0x4020 && addr < 0x6000) {
			return memory.exprom[addr - 0x4020];
		}

		// Delegate to mapper
		else if (addr >= 0x6000) {
			return memory.mapper.read(addr);
		}

		// Invalid or not implemented
		else {
			return Memory_InvalidRead(MAP_CPU, addr);
		}
	}

	// PPU memory map
	else {
		addr %= 0x4000;

		// Mapper
		if (addr < 0x2000) {
			return memory.mapper.read(addr);
		}

		// Nametables and palette
		switch (addr & 0xC00) {
			case 0x0:
				return memory.nametable0[addr & 0x3FF] ;
				break;

			case 0x400:
				return memory.nametable1[addr & 0x3FF];
				break;

			case 0x800:
				return memory.nametable2[addr & 0x3FF];
				break;

			case 0xC00:
				if (addr < 0x3F00) {
					return memory.nametable3[addr & 0x3FF];
				} else {
					// Palette RAM indices + mirrors
					addr &= 0x1F;

					if (addr >= 0x10 && ((addr & 3) == 0)) {
						addr -= 0x10;
					}

					return memory.paletteram[addr];
				}
				break;

			// Invalid or not implemented
			default:
				Memory_InvalidRead(MAP_PPU, addr);
				break;
		}
	}

	return 0;
}

WORD Memory_ReadWord(int map, WORD addr) {
	return (WORD)Memory_ReadByte(map, addr) | ((WORD)Memory_ReadByte(map, addr + 1) << 8);
}

void Memory_WriteByte(int map, WORD addr, BYTE val) {
	// CPU memory map
	if (map == MAP_CPU) {
		// Working RAM + mirrors
		if (addr < 0x2000) {
			memory.ram[addr % 0x800] = val;
		}

		// PPU control registers + mirrors
		else if (addr >= 0x2000 && addr < 0x4000) {
			switch (addr) {
				case 0x2000: // Controller
					PPU_WriteController(val);
					break;
				case 0x2001: // Mask
					PPU_WriteMask(val);
					break;
				case 0x2003: // OAM address
					PPU_WriteOAMAddress(val);
					break;
				case 0x2004: // OAM data
					PPU_WriteOAMData(val);
					break;
				case 0x2005: // Scroll
					PPU_WriteScroll(val);
					break;
				case 0x2006: // Address
					PPU_WriteAddress(val);
					break;
				case 0x2007: // Data
					PPU_WriteData(val);
					break;
				default:
					Memory_InvalidWrite(MAP_CPU, addr, val);
					break;
			}
		}

		// OAM DMA
		else if (addr == 0x4014) {
			PPU_WriteOAMDMA(val);
		}

		// APU registers
		else if (addr >= 0x4000 && addr < 0x4020) {
			// APU_WriteRegister(addr, val);
		}

		// Cartridge expansion ROM
		else if (addr >= 0x4020 && addr < 0x6000) {
			memory.exprom[addr - 0x4020] = val;
		}

		// Delegate to mapper
		else if (addr >= 0x6000) {
			memory.mapper.write(addr, val);
		}

		// Invalid or not implemented
		else {
			Memory_InvalidWrite(MAP_CPU, addr, val);
		}
	}

	// PPU memory map
	else if (map == MAP_PPU) {
		addr %= 0x4000;

		// Mapper
		if (addr < 0x2000) {
			memory.mapper.write(addr, val);
		}

		// Nametables and palette
		switch (addr & 0xC00) {
			case 0x0:
				memory.nametable0[addr & 0x3FF] = val;
				break;

			case 0x400:
				memory.nametable1[addr & 0x3FF] = val;
				break;

			case 0x800:
				memory.nametable2[addr & 0x3FF] = val;
				break;

			case 0xC00:
				if (addr >= 0x3F00 && addr <= 0x3FFF) {
					// Palette RAM indices + mirrors
					addr &= 0x1F;

					if (addr >= 0x10 && ((addr & 3) == 0)) {
						addr -= 0x10;
					}

					memory.paletteram[addr] = val & 0x3F;
				} else {
					memory.nametable3[addr & 0x3FF] = val;
				}
				break;

			// Invalid or not implemented
			default:
				Memory_InvalidWrite(MAP_PPU, addr, val);
				break;
		}
	}
}

void Memory_WriteWord(int map, WORD addr, WORD val) {
	Memory_WriteByte(map, addr, (BYTE)(val & 0x00FF));
	Memory_WriteByte(map, addr + 1, (BYTE)((val & 0xFF00) >> 8));
}

BYTE Memory_InvalidRead(int map, WORD addr) {
	if (map == MAP_CPU) {
		printf("WARNING: Invalid read at address 0x%4X (CPU map)\n", addr);
	} else {
		printf("WARNING: Invalid read at address 0x%4X (PPU map)\n", addr);
	}
	return 0;
}

void Memory_InvalidWrite(int map, WORD addr, BYTE val) {
	if (map == MAP_CPU) {
		printf("WARNING: Invalid write at address 0x%4X, value 0x%2X (CPU map)\n", addr, val);
	} else {
		printf("WARNING: Invalid write at address 0x%4X, value 0x%2X (PPU map)\n", addr, val);
	}
}

// TODO: add force read?
#ifdef DEBUG_MODE
void Memory_Dump() {
	BYTE dump[0xFFFF];

	for (int i = 0; i < 0xFFFF; i++) {
		dump[i] = Memory_ReadByte(MAP_CPU, (WORD)i);
	}

	// Dump raw data
	FILE* fp = fopen("memory.dump", "wb");

	if (!fp) {
		printf("Error opening memory.dump for output.\n");
		return;
	}

	fwrite(dump, sizeof(dump), 1, fp);

	fclose(fp);
}
#endif
