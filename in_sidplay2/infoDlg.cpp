#include <windows.h>
#include "infodlg.h"
#include "resource.h"
#include "threadsidplayer.h"

extern CThreadSidPlayer *sidPlayer;

int CALLBACK InfoDlgWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static int ismychange = 0;
	switch(uMsg)
	{
		case WM_INITDIALOG:
		{
			SetWindowLongPtr(hWnd, GWLP_USERDATA, lParam);

			SidTuneInfo *tuneInfo = reinterpret_cast<SidTuneInfo*>(lParam);
			char buf[20] = {0};
			std::string infoStr;
			int i;

			SetDlgItemTextA(hWnd,IDC_STIL_ED,sidPlayer->GetSTILData(infoStr.c_str()));

			SetDlgItemTextA(hWnd,IDC_FORMATSTC,tuneInfo->formatString());
			_snprintf(buf,20,"$%x",tuneInfo->loadAddr());
			SetDlgItemTextA(hWnd,IDC_LOADADDR_STC,buf);
			_snprintf(buf,20,"$%x",tuneInfo->initAddr());
			SetDlgItemTextA(hWnd,IDC_INITADDR_STC,buf);
			_snprintf(buf,20,"$%x",tuneInfo->playAddr());
			SetDlgItemTextA(hWnd,IDC_PLAYADDR_STC,buf);
			SetDlgItemTextA(hWnd,IDC_SUBSONGS_STC,itoa(tuneInfo->songs(),buf,10));
			_snprintf(buf,20,"$%x",tuneInfo->sidChipBase(1));
			SetDlgItemTextA(hWnd,IDC_SID2_ADDR,buf);
			_snprintf(buf,20,"$%x",tuneInfo->sidChipBase(2));
			SetDlgItemTextA(hWnd,IDC_SID3_ADDR,buf);

			switch(tuneInfo->sidModel(0))
			{
				case SidTuneInfo::SIDMODEL_6581: //SID2_MOS6581:
				{
					SetDlgItemTextA(hWnd,IDC_SIDMODEL_STC,"MOS6581");
					break;
				}
				case SidTuneInfo::SIDMODEL_8580:
				{
					SetDlgItemTextA(hWnd,IDC_SIDMODEL_STC,"MOS8580");
					break;
				}
				default:
				{
					SetDlgItemTextA(hWnd,IDC_SIDMODEL_STC,"unknown");
					break;
				}
			}

			switch (tuneInfo->clockSpeed())
			{
				case SidTuneInfo::CLOCK_PAL:
				{
					SetDlgItemTextA(hWnd, IDC_CLOCKSPEED_STC, "PAL");
					break;
				}
				case SidTuneInfo::CLOCK_NTSC:
				{
					SetDlgItemTextA(hWnd, IDC_CLOCKSPEED_STC, "NTSC");
					break;
				}
				default:
				{
					SetDlgItemTextA(hWnd, IDC_CLOCKSPEED_STC, "ANY");
					break;
				}
			}
			SetDlgItemTextA(hWnd,IDC_FILELENGTH_STC,itoa(tuneInfo->dataFileLen(),buf,10));

			/*infoStr.clear();
			for(i = 0; i < tuneInfo->numberOfInfoStrings(); ++i)
			{
				if((tuneInfo->infoString(i) == NULL) || (strlen(tuneInfo->infoString(i)) == 0)) continue;
				infoStr.append(tuneInfo->infoString(i));
				infoStr.append("\r\n");
			}

			for(i = 0; i < tuneInfo->numberOfCommentStrings(); ++i)
			{
				if(tuneInfo->commentString(i) == NULL) continue;
				infoStr.append(tuneInfo->commentString(i));
				infoStr.append("\r\n");
			}
			SetDlgItemTextA(hWnd,IDC_INOFBOX_STC,infoStr.c_str());*/

			//tuneInfo->speedString
			//tuneInfo->dataFileLen
			//tuneInfo->numberOfInfoStrings
			break;
		}
		case WM_DESTROY:
		{
			// TODO
			/*SidTuneInfo *info = (SidTuneInfo *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (info)
			{
				delete info;
			}*/
		}
	}

	return FALSE;
}