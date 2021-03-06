#include "famicom/ppu.h"
#include "famicom/cpu.h"
#include "utils/clock.h"
#include <string.h>

PPU ppu;
extern CPU cpu;
extern Memory memory;

#define SCANLINE_MAX 262

// Standard Famicom colour palette
// 0xFF0000 R
// 0x00FF00 G
// 0x0000FF B
static DWORD colours[] = {
	0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
	0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
	0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
	0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
	0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
	0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
	0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
	0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000,
};

// Clear and reinitialize data in PPU frame buffers
static void PPU_ClearBuffers() {
	if (ppu.buffer_front) {
		free(ppu.buffer_front);
	}

	if (ppu.buffer_back) {
		free(ppu.buffer_back);
	}

	ppu.buffer_front = (RGBA*)calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(RGBA));
	ppu.buffer_back = (RGBA*)calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(RGBA));

	for (int i = 0; i < SCREEN_HEIGHT; i++) {
		for (int j = 0; j < SCREEN_WIDTH; j++) {
			int index = i * SCREEN_WIDTH + j;
			ppu.buffer_front[index].r = 0;
			ppu.buffer_front[index].g = 0;
			ppu.buffer_front[index].b = 0;
			ppu.buffer_front[index].a = 255;
			ppu.buffer_back[index].r = 0;
			ppu.buffer_back[index].g = 0;
			ppu.buffer_back[index].b = 0;
			ppu.buffer_back[index].a = 255;
		}
	}
}

// Initialize upon power on
void PPU_Init() {
	// OAM data
	if (ppu.oam != NULL) {
		free(ppu.oam);
	}
	ppu.oam = (BYTE*)calloc(0x100, sizeof(BYTE));

	// Registers
	ppu.controller = 0;
	ppu.mask = 0;
	ppu.status = 0xA0;
	ppu.oam_addr = 0;
	ppu.scroll = 0;
	ppu.vram_addr = 0;
	ppu.vram_data = 0;
	ppu.odd_frame = 0;

	// Controller
	ppu.nametable_addr = 0;
	ppu.vram_addr_inc = 1;
	ppu.sprite_pattern_addr = 0;
	ppu.bg_pattern_addr = 0;
	ppu.sprite_height = 8;
	ppu.nmi_on_vblank = 0;

	// Mask
	ppu.grayscale = 0;
	ppu.show_bg_left = 1;
	ppu.show_bg = 1;
	ppu.show_sprites_left = 1;
	ppu.show_sprites = 1;
	ppu.emphasize_red = 0;
	ppu.emphasize_green = 0;
	ppu.emphasize_blue = 0;

	// Scroll
	ppu.first_write = 1;
	ppu.vram_addr_temp = 0;
	ppu.fine_x = 0;

	// Palettes
	for (int i = 0; i < NUM_PALETTES; i++) {
		ppu.palettes[i].r = (BYTE)(colours[i] >> 16);
		ppu.palettes[i].g = (BYTE)(colours[i] >> 8);
		ppu.palettes[i].b = (BYTE)colours[i];
		ppu.palettes[i].a = 255;
	}

	// Sprite
	ppu.sprite_0hit = 0;
	ppu.sprite_overflow = 0;
	ppu.sprite_count = 0;
	for (int i = 0; i < 8; i++) {
		ppu.sprite_patterns[i] = 0;
		ppu.sprite_positions[i] = 0;
		ppu.sprite_priorities[i] = 0;
		ppu.sprite_indices[i] = 0;
	}

	// Temporary variables + counters
	ppu.vram_addr_temp = 0;
	ppu.cycle = 0;
	ppu.scanline = 0;
	ppu.frame = 0;
	ppu.vblank = 0;
	ppu.nametable_byte = 0;
	ppu.attributetable_byte = 0;
	ppu.tile_byte_low = 0;
	ppu.tile_byte_high = 0;
	ppu.buffered_data = 0;

	// Clear the frame buffers
	PPU_ClearBuffers();

	// FPS
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ppu.frame_time);
	ppu.fps = 0;
}

// Reinitialize some variables after a reset
void PPU_Reset() {
/*
	PPUCTRL ($2000) 					0000 0000
	PPUMASK ($2001) 					0000 0000
	PPUSTATUS ($2002) 					U??x xxxx
	OAMADDR ($2003) 					unchanged
	$2005 / $2006 latch 				cleared
	PPUSCROLL ($2005) 					$0000
	PPUADDR ($2006) 					unchanged
	PPUDATA ($2007) read buffer 		$00
	odd frame 							no
	OAM 								unspecified
	Palette 							unchanged
	NT RAM (external, in Control Deck) 	unchanged
	CHR RAM (external, in Game Pak)	 	unchanged 
*/
	// Controller
	ppu.nametable_addr = 0;
	ppu.vram_addr_inc = 1;
	ppu.sprite_pattern_addr = 0;
	ppu.bg_pattern_addr = 0;
	ppu.sprite_height = 8;
	ppu.nmi_on_vblank = 0;

	// Mask
	ppu.grayscale = 0;
	ppu.show_bg_left = 1;
	ppu.show_bg = 1;
	ppu.show_sprites_left = 1;
	ppu.show_sprites = 1;
	ppu.emphasize_red = 0;
	ppu.emphasize_green = 0;
	ppu.emphasize_blue = 0;

	// Temporary variables + counters
	ppu.cycle = CYCLE_STEP_END;
	ppu.scanline = SCANLINE_POSTLINE;
	ppu.frame = 0;

	// Misc
	ppu.first_write = 1;
	ppu.odd_frame = 0;
	ppu.fine_x = 0;
	ppu.buffered_data = 0;
	PPU_ClearBuffers();

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ppu.frame_time);
	ppu.fps = 0;
}

static void PPU_NMIChange() {
	BYTE nmi = ppu.nmi_output && ppu.nmi_occurred;

	if (nmi && !ppu.nmi_previous) {
		// TODO fix this timing
		ppu.nmi_delay = 15;
	}

	ppu.nmi_previous = nmi;
}

// $2000 - PPUCTRL
void PPU_WriteController(BYTE val) {
/*
	7654 3210
	|||| ||||
	|||| ||++- Base nametable address
	|||| ||    (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)
	|||| |+--- VRAM address increment per CPU read/write of PPUDATA
	|||| |     (0: add 1, going across; 1: add 32, going down)
	|||| +---- Sprite pattern table address for 8x8 sprites
	||||       (0: $0000; 1: $1000; ignored in 8x16 mode)
	|||+------ Background pattern table address (0: $0000; 1: $1000)
	||+------- Sprite size (0: 8x8; 1: 8x16)
	|+-------- PPU master/slave select
	|          (0: read backdrop from EXT pins; 1: output color on EXT pins)
	+--------- Generate an NMI at the start of the
               vertical blanking interval (0: off; 1: on)
*/
	ppu.controller = val;

	ppu.nametable_addr = val & 0x3;
	ppu.vram_addr_inc = ((val >> 2) & 0x1) ? 1: 32;
	ppu.sprite_pattern_addr = (val >> 3) & 0x1;
	ppu.bg_pattern_addr = (val >> 4) & 0x1;
	ppu.sprite_height = ((val >> 5) & 0x1) ? 8 : 16;
	ppu.master_slave = (val >> 6) & 0x1;
	ppu.nmi_on_vblank = ((val >> 7) & 0x1) == 1;
	PPU_NMIChange();

	// Change scroll latch to match base nametable address
	ppu.vram_addr_temp = (ppu.vram_addr_temp & 0xF3FF) | (((WORD)val & 0x3) << 10);
}

// $2001 - PPUMASK
void PPU_WriteMask(BYTE val) {
/*
	7654 3210
	|||| ||||
	|||| |||+- Grayscale (0: normal color, 1: produce a greyscale display)
	|||| ||+-- 1: Show background in leftmost 8 pixels of screen, 0: Hide
	|||| |+--- 1: Show sprites in leftmost 8 pixels of screen, 0: Hide
	|||| +---- 1: Show background
	|||+------ 1: Show sprites
	||+------- Emphasize red*
	|+-------- Emphasize green*
	+--------- Emphasize blue*
*/
	ppu.grayscale = val & 0x1;
	ppu.show_bg_left = (val >> 1) & 0x1;
	ppu.show_sprites_left = (val >> 2) & 0x1;
	ppu.show_bg = (val >> 3) & 0x1;
	ppu.show_sprites = (val >> 4) & 0x1;
	ppu.emphasize_red = (val >> 5) & 0x1;
	ppu.emphasize_green = (val >> 6) & 0x1;
	ppu.emphasize_blue = (val >> 7) & 0x1;
}

// $2002 - PPUSTATUS
BYTE PPU_ReadStatus() {
	BYTE status = ppu.status & 0x1F;
	status |= ppu.sprite_overflow << 5;
	status |= ppu.sprite_0hit << 6;

	if (ppu.nmi_occurred) {
		status |= 1 << 7;
	}

	ppu.nmi_occurred = 0;
	PPU_NMIChange();

	ppu.first_write = 1;
	return status;
}

// $2003 - OAMADDR
void PPU_WriteOAMAddress(BYTE val) {
	ppu.oam_addr = val;
}

// $2004 - OAMDATA
void PPU_WriteOAMData(BYTE val) {
	ppu.oam[ppu.oam_addr++] = val;
}

// $2004 - OAMDATA
BYTE PPU_ReadOAMData() {
	return ppu.oam[ppu.oam_addr];
}

// $2005 - PPUSCROLL
void PPU_WriteScroll(BYTE val) {
	if (ppu.first_write) {
		// Horizontal scroll offset
		ppu.fine_x = val & 0x7;
		ppu.vram_addr_temp = (ppu.vram_addr_temp & 0xFFE0) | ((WORD)val >> 3);
		ppu.first_write = 0;
	} else {
		// Vertical scroll offset
		ppu.vram_addr_temp = (ppu.vram_addr_temp & 0x8FFF) | (((WORD)val & 0x07) << 12);
		ppu.vram_addr_temp = (ppu.vram_addr_temp & 0xFC1F) | (((WORD)val & 0xF8) << 2);
		ppu.first_write = 1;
	}
}

// $2006 - PPUADDR
void PPU_WriteAddress(BYTE val) {
	if (ppu.first_write) {
		// Clear bits 14-8 (and 15), save 5-0 of val to 13-8 of temp
		ppu.vram_addr_temp = (ppu.vram_addr_temp & 0x80FF) | (((WORD)val & 0x3F) << 8);
		ppu.first_write = 0;
	} else {
		// Copy lower 8 bits to temp, set addr to temp value
		ppu.vram_addr_temp = (ppu.vram_addr_temp & 0xFF00) | (WORD)val;
		ppu.vram_addr = ppu.vram_addr_temp;
		ppu.first_write = 1;
	}
}

// $2007 - PPUDATA
BYTE PPU_ReadData() {
	BYTE val = Memory_ReadByte(MAP_PPU, ppu.vram_addr);

	if ((ppu.vram_addr % 0x4000) < 0x3F00) {
		BYTE buffered_data = ppu.buffered_data;
		ppu.buffered_data = val;
		val = buffered_data;
	} else {
		ppu.buffered_data = Memory_ReadByte(MAP_PPU, ppu.vram_addr - 0x1000);
	}

	ppu.vram_addr += ppu.vram_addr_inc;

	return val;
}

// $2007 - PPUDATA
void PPU_WriteData(BYTE val) {
	Memory_WriteByte(MAP_PPU, ppu.vram_addr, val);
	ppu.vram_addr += ppu.vram_addr_inc;
}

// $4014 - OAMDMA
void PPU_WriteOAMDMA(BYTE val) {
	WORD addr = (WORD)val << 8;

	for (int i = 0; i < 256; i++) {
		ppu.oam[ppu.oam_addr++] = Memory_ReadByte(MAP_CPU, addr);
		addr++;
	}

	if (cpu.cycles % 2 == 0) {
		CPU_Suspend(514);
	} else {
		CPU_Suspend(513);
	}
}

// Read from the palette register
BYTE PPU_ReadPalette(WORD addr) {
	if (addr >= 16 && addr % 4 == 0) {
		addr -= 16;
	}

	return memory.paletteram[addr];
}

// Write to the palette register
void PPU_WritePalette(WORD addr, BYTE val) {
	if (addr >= 16 && addr % 4 == 0) {
		addr -= 16;
	}

	memory.paletteram[addr] = val;
}

// Unpack data for the current tile
static DWORD PPU_GetTileData() {
	return (DWORD)(ppu.tile >> 32);
}

static BYTE PPU_GetBackgroundPixel() {
	if (!ppu.show_bg) {
		return 0;
	}

	BYTE pixel = PPU_GetTileData() >> ((7 - ppu.fine_x) * 4);
	return (BYTE)(pixel & 0x0F);
}

static BYTE PPU_GetSpritePixel(int* num) {
	if (!ppu.show_sprites) {
		*num = 0;
		return 0;
	}

	for (int i = 0; i < ppu.sprite_count; i++) {
		int offset = (ppu.cycle - 1) - (int)(ppu.sprite_positions[i]);

		if (offset < 0 || offset > 7) {
			continue;
		}

		offset = 7 - offset;

		BYTE colour = (BYTE)((ppu.sprite_patterns[i] >> (BYTE)(offset * 4)) & 0x0F);

		if (colour % 4 == 0) {
			continue;
		}

		*num = (BYTE)i;
		return colour;
	}

	*num = 0;
	return 0;
}

static DWORD PPU_GetSpritePattern(int index, int row) {
	BYTE tile = ppu.oam[index * 4 + 1];
	BYTE attr = ppu.oam[index * 4 + 2];
	WORD addr = 0;

	if (ppu.sprite_height == 0) {
		if ((attr & 0x80) == 0x80) {
			row = 7 - row;
		}

		addr = 0x1000 * (WORD)ppu.sprite_pattern_addr + ((WORD)tile * 16) + (WORD)row;
	} else {
		if ((attr & 0x80) == 0x80) {
			row = 15 - row;
		}

		tile &= 0xFE;

		if (row > 7) {
			tile++;
			row -= 8;
		}

		addr = 0x1000 * (tile & 1) + ((WORD)tile * 16) + (WORD)row;
	}

	int a = (attr & 3) << 2;
	BYTE low_tile = Memory_ReadByte(MAP_PPU, addr);
	BYTE high_tile = Memory_ReadByte(MAP_PPU, addr + 8);

	DWORD data = 0;

	for (int i = 0; i < 8; i++) {
		BYTE low = 0;
		BYTE high = 0;

		if ((attr & 0x40) == 0x40) {
			low = low_tile & 1;
			high = (high_tile & 1) << 1;
			low_tile >>= 1;
			high_tile >>= 1;
		} else {
			low = (low_tile & 0x80) >> 7;
			high = (high_tile & 0x80) >> 6;
			low_tile <<= 1;
			high_tile <<= 1;
		}

		data <<= 4;
		data |= (DWORD)(a | low | high);
	}

	return data;
}

static void PPU_EvaluateSprites() {
	int height = ppu.sprite_height == 0 ? 8 : 16;
	int count = 0;

	for (int i = 0; i < 64; i++) {
		BYTE y = ppu.oam[i * 4];
		BYTE a = ppu.oam[i * 4 + 2];
		BYTE x = ppu.oam[i * 4 + 3];
		int row = ppu.scanline - y;

		if (row < 0 || row >= height) {
			continue;
		}

		if (count < 8) {
			ppu.sprite_patterns[count] = PPU_GetSpritePattern(i, row);
			ppu.sprite_positions[count] = x;
			ppu.sprite_priorities[count] = (a >> 5) & 1;
			ppu.sprite_indices[count] = (BYTE)i;
		}

		count++;
	}

	if (count > 8) {
		count = 8;
		ppu.sprite_overflow = 1;
	}

	ppu.sprite_count = count;
}

static void PPU_RenderPixel() {
	int x = ppu.cycle - 1;
	int y = ppu.scanline;

	BYTE bgpixel = PPU_GetBackgroundPixel();

	int spritepixel_num = 0;
	BYTE spritepixel = PPU_GetSpritePixel(&spritepixel_num);

	if (x < 8) {
		if (!ppu.show_bg_left) {
			bgpixel = 0;
		}

		if (!ppu.show_sprites_left) {
			spritepixel = 0;
		}
	}

	int show_bgpixel = bgpixel % 4 != 0;
	int show_spritepixel = spritepixel % 4 != 0;

	BYTE colour = 0;

	if (!show_bgpixel && !show_spritepixel) {
		colour = 0;
	} else if (!show_bgpixel && show_spritepixel) {
		colour = spritepixel | 0x10;
	} else if (show_bgpixel && !show_spritepixel) {
		colour = bgpixel;
	} else {
		if (ppu.sprite_indices[spritepixel_num] == 0 && x < 255) {
			ppu.sprite_0hit = 1;
		}

		if (ppu.sprite_priorities[spritepixel_num] == 0) {
			colour = spritepixel | 0x10;
		} else {
			colour = bgpixel;
		}
	}

	RGBA palette = ppu.palettes[PPU_ReadPalette((WORD)colour) % 64];
	int index = y * SCREEN_WIDTH + x;
	ppu.buffer_back[index].r = palette.r;
	ppu.buffer_back[index].g = palette.g;
	ppu.buffer_back[index].b = palette.b;
	ppu.buffer_back[index].a = palette.a;
}

static void PPU_GetFPS() {
	struct timespec new_time;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &new_time);
	ppu.frame_delta = Clock_Diff(ppu.frame_time, new_time);
	double dt = (double)ppu.frame_delta.tv_nsec / 1000000000.0;
	ppu.fps = 1.0 / dt;
	memcpy(&ppu.frame_time, &new_time, sizeof(struct timespec));
}

void PPU_Step() {
	// Decrement NMI timer
	if (ppu.nmi_delay > 0) {
		ppu.nmi_delay--;

		// Trigger an NMI if timer reaches 0
		if (ppu.nmi_delay == 0 && ppu.nmi_output && ppu.nmi_occurred) {
			CPU_Interrupt_NMI();
		}
	}

	do {
		// Increment frame counter if the frame has been completed
		if (ppu.show_bg && ppu.show_sprites) {
			if (ppu.odd_frame && ppu.scanline == SCANLINE_FRAME_END && ppu.cycle == CYCLE_FRAME_END) {
				ppu.cycle = 0;
				ppu.scanline = 0;
				ppu.frame++;
				ppu.odd_frame ^= 1;
				PPU_GetFPS();
				break;
			}
		}

		// Increment the cycle counter
		ppu.cycle++;

		// Check whether the cycle counter has reached the end of the step
		if (ppu.cycle > CYCLE_STEP_END) {
			ppu.cycle = 0;
			ppu.scanline++;

			if (ppu.scanline > SCANLINE_FRAME_END) {
				ppu.scanline = 0;
				ppu.frame++;
				ppu.odd_frame ^= 1;
				PPU_GetFPS();
			}
		}
	} while (0);

	// Determine where the current scanline is in the render sequence
	int line_preline = ppu.scanline == SCANLINE_PRELINE;
	int line_postline = ppu.scanline == SCANLINE_POSTLINE;
	int line_visible = ppu.scanline <= SCANLINE_VISIBLE_END;
	int line_render = line_preline || line_visible;
	int line_vblank = ppu.scanline == SCANLINE_VBLANK_BEGIN;

	// Determine where the current cycle is in the render sequence
	int cycle_begin = ppu.cycle == CYCLE_BEGIN;
	int cycle_prefetch = ppu.cycle >= CYCLE_PREFETCH_BEGIN && ppu.cycle <= CYCLE_PREFETCH_END;
	int cycle_visible = ppu.cycle >= CYCLE_VISIBLE_BEGIN && ppu.cycle <= CYCLE_VISIBLE_END;
	int cycle_fetch = cycle_prefetch || cycle_visible;
	int cycle_copy_y = ppu.cycle >= CYCLE_COPY_Y_BEGIN && ppu.cycle <= CYCLE_COPY_Y_END;
	int cycle_sprite = ppu.cycle == CYCLE_EVALUATE_SPRITE;

	// Only render if the PPU has BG or sprites visible
	// if (ppu.show_bg || ppu.show_sprites) {
	if (1) {
		// Render if the current scanline and cycle are part of the visible area of the screen
		if (line_visible && cycle_visible) {
			PPU_RenderPixel();
		}

		// Fetch cycle
		if (line_render && cycle_fetch) {
			ppu.tile <<= 4;

			// Temp variables
			BYTE shift = 0;
			BYTE fine_y = 0;
			BYTE low = 0;
			BYTE high = 0;
			WORD addr = 0;
			DWORD data = 0;

			switch (ppu.cycle % 8) {
				// Fetch nametable byte
				case FETCH_NAMETABLE:
					addr = 0x2000 | (ppu.vram_addr & 0x0FFF);
					ppu.nametable_byte = Memory_ReadByte(MAP_PPU, addr);
					break;

				// Fetch attribute table byte
				case FETCH_ATTRIBUTETABLE:
					addr = 0x23C0 | (ppu.vram_addr & 0x0C00) | ((ppu.vram_addr >> 4) & 0x38) | ((ppu.vram_addr >> 2) & 0x07);
					shift = ((ppu.vram_addr >> 4) & 4) | (ppu.vram_addr & 2);
					ppu.attributetable_byte = ((Memory_ReadByte(MAP_PPU, addr) >> shift) & 3) << 2;
					break;

				// Fetch tile low byte
				case FETCH_TILE_LOW:
					fine_y = (ppu.vram_addr >> 12) & 7;
					addr = 0x1000 * (WORD)ppu.bg_pattern_addr + (WORD)ppu.nametable_byte * 16 + fine_y;
					ppu.tile_byte_low = Memory_ReadByte(MAP_PPU, addr);
					break;

				// Fetch tile high byte
				case FETCH_TILE_HIGH:
					fine_y = (ppu.vram_addr >> 12) & 7;
					addr = 0x1000 * (WORD)ppu.bg_pattern_addr + (WORD)ppu.nametable_byte * 16 + fine_y;
					ppu.tile_byte_high = Memory_ReadByte(MAP_PPU, addr + 8);
					break;

				// Store tile data
				case FETCH_STORE:
					data = 0;
					for (int i = 0; i < 8; i++) {
						low = (ppu.tile_byte_low & 0x80) >> 7;
						high = (ppu.tile_byte_high & 0x80) >> 6;
						ppu.tile_byte_low <<= 1;
						ppu.tile_byte_high <<= 1;
						data <<= 4;
						data |= (DWORD)(ppu.attributetable_byte | low | high);
					}
					ppu.tile |= (QWORD)data;
					break;
			}
		}

		// Copy Y
		if (line_preline && cycle_copy_y) {
			ppu.vram_addr = (ppu.vram_addr & 0x841F) | (ppu.vram_addr_temp & 0x7BE0);
		}

		if (line_render) {
			// Increment X
			if (cycle_fetch && ppu.cycle % 8 == 0) {
				if ((ppu.vram_addr & 0x001F) == 31) {
					ppu.vram_addr &= 0xFFE0;
					ppu.vram_addr ^= 0x0400;
				} else {
					ppu.vram_addr++;
				}
			}

			// Increment Y
			if (ppu.cycle == CYCLE_INCREMENT_Y) {
				if ((ppu.vram_addr & 0x7000) != 0x7000) {
					ppu.vram_addr += 0x1000;
				} else {
					ppu.vram_addr &= 0x8FFF;
					WORD y = (ppu.vram_addr & 0x03E0) >> 5;

					if (y == 29) {
						y = 0;
						ppu.vram_addr ^= 0x0800;
					} else if (y == 31) {
						y = 0;
					} else {
						y++;
					}
					ppu.vram_addr = (ppu.vram_addr & 0xFC1F) | (y << 5);
				}
			}

			// Copy X
			if (ppu.cycle == CYCLE_COPY_X) {
				ppu.vram_addr = (ppu.vram_addr & 0xFBE0) | (ppu.vram_addr_temp & 0x041F);
			}
		}

		// Evaluate sprites
		if (cycle_sprite) {
			if (line_visible) {
				PPU_EvaluateSprites();
			} else {
				ppu.sprite_count = 0;
			}
		}
	}

	// Set VBlank (swap buffers)
	if (line_vblank && cycle_begin) {
		size_t num = SCREEN_WIDTH * SCREEN_HEIGHT;
		size_t size = num * sizeof(RGBA);
		RGBA* buf = (RGBA*)calloc(num, sizeof(RGBA));
		memcpy(buf, ppu.buffer_front, size);
		memcpy(ppu.buffer_front, ppu.buffer_back, size);
		memcpy(ppu.buffer_back, buf, size);
		free(buf);
		ppu.nmi_occurred = 1;
		PPU_NMIChange();
	}

	// Clear VBlank
	if (line_preline && cycle_begin) {
		ppu.nmi_occurred = 0;
		PPU_NMIChange();
		ppu.sprite_0hit = 0;
		ppu.sprite_overflow = 0;
	}
}
