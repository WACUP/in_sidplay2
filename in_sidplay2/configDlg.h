#pragma once

//#include <sidplay/sidplay2.h>
#include "threadsidplayer.h"

extern CThreadSidPlayer *sidPlayer;

int CALLBACK ConfigDlgWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

//! Initialize dialog controls, fill it with data from config structure
void ConfigDlgInitDialog(HWND hWnd);

//! Copy values from controls to config structure
void UpdateConfig(HWND hWnd);

//! Procedure handling selection of song length file
void SelectHvscFile(HWND hWnd);

//! Procedure handling selection of HVSC directory
void SelectHvscDirectory(HWND hWnd);

const bool create_sid_player(const bool skip_if_not_already_created);