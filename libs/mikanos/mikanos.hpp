/**
 * @file mikanos.hpp
 *
 * MikanOSが提供する関数
 */

#pragma once

#include "../common/message.hpp"
#include "../kinos/syscall.h"

/**
 * @brief ウィンドウを作成する 返り値はlayer_idでウィンドウを操作するような関数はこれを用いる -1はエラー
 * 
 * @param w 
 * @param h 
 * @param x 
 * @param y 
 * @return int layer_id 
 */
int OpenWindow(int w, int h, int x, int y) {
    Message msg{Message::aOpenWindow};
    msg.arg.openwindow.w = w;
    msg.arg.openwindow.h = h;
    msg.arg.openwindow.x = x;
    msg.arg.openwindow.y = y;
    SyscallSendMessageToOs(&msg);
    Message rmsg[1];
    int layer_id;
    while (true) {
        auto [ n, err ] = SyscallReceiveMessage(rmsg, 1);
            if (err) {
                layer_id = -1;
                break;
            }

            if (rmsg[0].type == Message::aLayerId) {
                layer_id = rmsg[0].arg.layerid.layerid;
                break;
            }
    }
    return layer_id;
}

/**
 * @brief ウィンドウ内で中を埋めた四角形を表示する
 * 
 * @param layer_id 
 * @param x 
 * @param y 
 * @param w 
 * @param h 
 * @param draw trueなら描画する falseならしない
 * @param color 
 */
void WinFillRectangle(int layer_id, int x, int y, int w, int h, bool draw, uint32_t color) {
    Message msg{Message::aWinFillRectangle};
    msg.arg.winfillrectangle.layer_id = layer_id;
    msg.arg.winfillrectangle.x = x;
    msg.arg.winfillrectangle.y = y;
    msg.arg.winfillrectangle.w = w;
    msg.arg.winfillrectangle.h = h;
    msg.arg.winfillrectangle.draw = draw;
    msg.arg.winfillrectangle.color = color;
    SyscallSendMessageToOs(&msg);
}

void WinWriteChar(int layer_id, int x, int y, uint32_t color, char c) {
    Message msg{Message::aWinWriteChar};
    msg.arg.winwritechar.layer_id = layer_id;
    msg.arg.winwritechar.x = x;
    msg.arg.winwritechar.y = y;
    msg.arg.winwritechar.color = color;
    msg.arg.winwritechar.c = c;
    SyscallSendMessageToOs(&msg);
}

void WinRedraw(int layer_id) {
    Message msg{Message::aWinRedraw};
    msg.arg.layerid.layerid = layer_id;
    SyscallSendMessageToOs(&msg);
}

void CloseWindow(int layer_id) {
    Message msg{Message::aCloseWindow};
    msg.arg.layerid.layerid = layer_id;
    SyscallSendMessageToOs(&msg);
}