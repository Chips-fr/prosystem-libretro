/* ----------------------------------------------------------------------------
 *   ___  ___  ___  ___       ___  ____  ___  _  _
 *  /__/ /__/ /  / /__  /__/ /__    /   /_   / |/ /
 * /    / \  /__/ ___/ ___/ ___/   /   /__  /    /  emulator
 *
 * ----------------------------------------------------------------------------
 * Copyright 2005 Greg Stanton
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 * Maria.c
 * ----------------------------------------------------------------------------
 */
#include "Maria.h"
#include "Equates.h"
#include "Pair.h"
#include "Memory.h"
#include "Sally.h"
#include "Cartridge.h"
#define MARIA_LINERAM_SIZE 160

rect maria_displayArea = {0, 16, 319, 258};
rect maria_visibleArea = {0, 26, 319, 248};
uint8_t maria_surface[MARIA_SURFACE_SIZE] = {0};
uint16_t maria_scanline = 1;

static uint8_t maria_lineRAM[MARIA_LINERAM_SIZE];
static uint32_t maria_cycles;
static pair maria_dpp;
static pair maria_dp;
static pair maria_pp;
static uint8_t maria_horizontal;
static uint8_t maria_palette;
static int8_t maria_offset;
static uint8_t maria_h08;
static uint8_t maria_h16;
static uint8_t maria_wmode;

static uint8_t maria_ReadByte(uint16_t address)
{
   uint32_t page, chrOffset;
   if(cartridge_type != CARTRIDGE_TYPE_SOUPER)
      return memory_ram[address];
   if((cartridge_souper_mode & CARTRIDGE_SOUPER_MODE_MFT) == 0 || address < 0x8000 ||
      ((cartridge_souper_mode & CARTRIDGE_SOUPER_MODE_CHR) == 0 && address < 0xc000))
   {
      return memory_Read(address);
   }
   if(address >= 0xc000) /* EXRAM */
      return memory_Read(address - 0x8000);
   if(address < 0xa000)  /* Fixed ROM */
      return memory_Read(address + 0x4000);
   page      = (uint16_t)cartridge_souper_chr_bank[(address & 0x80) != 0? 1: 0];
   chrOffset = (((page & 0xfe) << 4) | (page & 1)) << 7;
   return cartridge_LoadROM((address & 0x0f7f) | chrOffset);
}

static void maria_StoreCell2(uint8_t data)
{
   if(maria_horizontal < MARIA_LINERAM_SIZE)
   {
      if(data)
         maria_lineRAM[maria_horizontal] = maria_palette | data;
      else
      {
         uint8_t kmode = maria_ReadByte(CTRL) & 4;
         if(kmode)
            maria_lineRAM[maria_horizontal] = 0;
      }
   }

   maria_horizontal++;
}

static void maria_StoreCell(uint8_t high, uint8_t low)
{
  if(maria_horizontal < MARIA_LINERAM_SIZE)
  {
    if(low || high)
      maria_lineRAM[maria_horizontal] = (maria_palette & 16) | high | low;
    else
    { 
      uint8_t kmode = maria_ReadByte(CTRL) & 4;
      if(kmode)
        maria_lineRAM[maria_horizontal] = 0;
    }
  }
  maria_horizontal++;
}

static bool maria_IsHolyDMA(void)
{
   if(maria_pp.w > 32767)
   {
      if(maria_h16 && (maria_pp.w & 4096))
         return true;
      if(maria_h08 && (maria_pp.w & 2048))
         return true;
   }
  return false;
}

static uint8_t maria_GetColor(uint8_t data)
{
  if(data & 3)
    return maria_ReadByte(BACKGRND + data);
  return maria_ReadByte(BACKGRND);
}

static void maria_StoreGraphic(void)
{
   uint8_t data = maria_ReadByte(maria_pp.w);
   if(maria_wmode)
   {
      if(maria_IsHolyDMA( ))
      {
         maria_StoreCell(0, 0);
         maria_StoreCell(0, 0);
      }
      else
      {
         maria_StoreCell((data & 12), (data & 192) >> 6);
         maria_StoreCell((data & 48) >> 4, (data & 3) << 2);
      }
   }
   else
   {
      if(maria_IsHolyDMA( ))
      {
         maria_StoreCell2(0);
         maria_StoreCell2(0);
         maria_StoreCell2(0);
         maria_StoreCell2(0);
      }
      else
      {
         maria_StoreCell2((data & 192) >> 6);
         maria_StoreCell2((data & 48) >> 4);
         maria_StoreCell2((data & 12) >> 2);
         maria_StoreCell2(data & 3);
      }
   }
   maria_pp.w++;
}

static void maria_WriteLineRAM(uint8_t* buffer)
{
   uint8_t rmode = maria_ReadByte(CTRL) & 3;

   if(rmode == 0)
   {
      int pixel = 0, index;

      for(index = 0; index < MARIA_LINERAM_SIZE; index += 4)
      {
         uint8_t color;
         color = maria_GetColor(maria_lineRAM[index + 0]);
         buffer[pixel++] = color;
         buffer[pixel++] = color;
         color = maria_GetColor(maria_lineRAM[index + 1]);
         buffer[pixel++] = color;
         buffer[pixel++] = color;
         color = maria_GetColor(maria_lineRAM[index + 2]);
         buffer[pixel++] = color;
         buffer[pixel++] = color;
         color = maria_GetColor(maria_lineRAM[index + 3]);
         buffer[pixel++] = color;
         buffer[pixel++] = color;
      }
   }
   else if(rmode == 2)
   {
      int pixel = 0, index;
      for(index = 0; index < MARIA_LINERAM_SIZE; index += 4)
      {
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 16) | ((maria_lineRAM[index + 0] & 8) >> 3) | ((maria_lineRAM[index + 0] & 2)));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 16) | ((maria_lineRAM[index + 0] & 4) >> 2) | ((maria_lineRAM[index + 0] & 1) << 1));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 16) | ((maria_lineRAM[index + 1] & 8) >> 3) | ((maria_lineRAM[index + 1] & 2)));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 16) | ((maria_lineRAM[index + 1] & 4) >> 2) | ((maria_lineRAM[index + 1] & 1) << 1));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 16) | ((maria_lineRAM[index + 2] & 8) >> 3) | ((maria_lineRAM[index + 2] & 2)));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 16) | ((maria_lineRAM[index + 2] & 4) >> 2) | ((maria_lineRAM[index + 2] & 1) << 1));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 16) | ((maria_lineRAM[index + 3] & 8) >> 3) | ((maria_lineRAM[index + 3] & 2)));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 16) | ((maria_lineRAM[index + 3] & 4) >> 2) | ((maria_lineRAM[index + 3] & 1) << 1));
      }
   }
   else if(rmode == 3)
   {
      int pixel = 0, index;
      for(index = 0; index < MARIA_LINERAM_SIZE; index += 4)
      {
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 30));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 0] & 28) | ((maria_lineRAM[index + 0] & 1) << 1));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 30));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 1] & 28) | ((maria_lineRAM[index + 1] & 1) << 1));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 30));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 2] & 28) | ((maria_lineRAM[index + 2] & 1) << 1));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 30));
         buffer[pixel++] = maria_GetColor((maria_lineRAM[index + 3] & 28) | ((maria_lineRAM[index + 3] & 1) << 1));
      }
   }
}

static void maria_StoreLineRAM(void)
{
   int index;
   uint8_t mode;

   for(index = 0; index < MARIA_LINERAM_SIZE; index++)
      maria_lineRAM[index] = 0;

   mode = maria_ReadByte(maria_dp.w + 1);

   while(mode & 0x5f)
   {
      uint8_t width;
      uint8_t indirect = 0;

      maria_pp.b.l = maria_ReadByte(maria_dp.w);
      maria_pp.b.h = maria_ReadByte(maria_dp.w + 2);

      if(mode & 31) { 
         maria_cycles += 8;
         maria_palette = (maria_ReadByte(maria_dp.w + 1) & 224) >> 3;
         maria_horizontal = maria_ReadByte(maria_dp.w + 3);
         width = maria_ReadByte(maria_dp.w + 1) & 31;
         width = ((~width) & 31) + 1;
         maria_dp.w += 4;
      }
      else { 
         maria_cycles += 10;
         maria_palette = (maria_ReadByte(maria_dp.w + 3) & 224) >> 3;
         maria_horizontal = maria_ReadByte(maria_dp.w + 4);
         indirect = maria_ReadByte(maria_dp.w + 1) & 32;
         maria_wmode = maria_ReadByte(maria_dp.w + 1) & 128;
         width = maria_ReadByte(maria_dp.w + 3) & 31;
         width = (width == 0)? 32: ((~width) & 31) + 1;
         maria_dp.w += 5;
      }

      if(!indirect)
      {
         int index;
         maria_pp.b.h += maria_offset;
         for(index = 0; index < width; index++)
         {
            maria_cycles += 3;
            maria_StoreGraphic( );
         }
      }
      else
      {
         int index;
         uint8_t cwidth = maria_ReadByte(CTRL) & 16;
         pair basePP = maria_pp;
         for(index = 0; index < width; index++)
         {
            maria_cycles += 3;
            maria_pp.b.l = maria_ReadByte(basePP.w++);
            maria_pp.b.h = maria_ReadByte(CHARBASE) + maria_offset;

            maria_cycles += 6;
            maria_StoreGraphic( );
            if(cwidth)
            {
               maria_cycles += 3;
               maria_StoreGraphic( );
            }
         }
      }
      mode = maria_ReadByte(maria_dp.w + 1);
   }
}

void maria_Reset(void)
{
   int index;
   maria_scanline = 1;
   for(index = 0; index < MARIA_SURFACE_SIZE; index++)
      maria_surface[index] = 0;
}

uint32_t maria_RenderScanline(void)
{
   maria_cycles = 0;
   if((maria_ReadByte(CTRL) & 96) == 64 && maria_scanline >= maria_displayArea.top && maria_scanline <= maria_displayArea.bottom)
   {
      maria_cycles += 31;
      if(maria_scanline == maria_displayArea.top)
      {
         maria_cycles += 7;
         maria_dpp.b.l = maria_ReadByte(DPPL);
         maria_dpp.b.h = maria_ReadByte(DPPH);
         maria_h08 = maria_ReadByte(maria_dpp.w) & 32;
         maria_h16 = maria_ReadByte(maria_dpp.w) & 64;
         maria_offset = maria_ReadByte(maria_dpp.w) & 15;
         maria_dp.b.l = maria_ReadByte(maria_dpp.w + 2);
         maria_dp.b.h = maria_ReadByte(maria_dpp.w + 1);

         if(maria_ReadByte(maria_dpp.w) & 128)
            sally_ExecuteNMI( );
      }
      else if(maria_scanline >= maria_visibleArea.top && maria_scanline <= maria_visibleArea.bottom)
         maria_WriteLineRAM(maria_surface + ((maria_scanline - maria_displayArea.top) * Rect_GetLength(&maria_displayArea)));

      if(maria_scanline != maria_displayArea.bottom)
      {
         maria_dp.b.l = maria_ReadByte(maria_dpp.w + 2);
         maria_dp.b.h = maria_ReadByte(maria_dpp.w + 1);
         maria_StoreLineRAM( );
         maria_offset--;
         if(maria_offset < 0)
         {
            maria_dpp.w += 3;
            maria_h08 = maria_ReadByte(maria_dpp.w) & 32;
            maria_h16 = maria_ReadByte(maria_dpp.w) & 64;
            maria_offset = maria_ReadByte(maria_dpp.w) & 15;

            if(maria_ReadByte(maria_dpp.w) & 128)
               sally_ExecuteNMI( );
         }
      }    
   }
   return maria_cycles;
}

void maria_Clear(void)
{
   int index;
   for(index = 0; index < MARIA_SURFACE_SIZE; index++)
      maria_surface[index] = 0;
}
