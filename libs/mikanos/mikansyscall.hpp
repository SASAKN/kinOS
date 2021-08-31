/**
 * @file mikansyscall.hpp
 *
 * MikanOSが提供するシステムコール
 */

#pragma once

#include "../common/message.hpp"
#include "../kinos/syscall.h"

int OpenWindow(int w, int h, int x, int y);
   
void WinFillRectangle(int layer_id, bool draw, int x, int y, int w, int h, uint32_t color);

void WinWriteChar(int layer_id, bool draw, int x, int y, uint32_t color, char c);

void WinWriteString(int layer_id, bool draw, int x, int y, uint32_t color, char* s);

void WinDrawLine(int layer_id, bool draw, int x0, int y0, int x1, int y1, uint32_t color);

void WinRedraw(int layer_id);

void CloseWindow(int layer_id);
