// in_sidplay2.cpp : Defines the exported functions for the DLL application.
//

#include <windows.h>
#include <sdk/winamp/in2.h>
#include "ThreadSidplayer.h"
#include "ThreadSidDecoder.h"
#include "resource.h"
#include "configdlg.h"
#include "infodlg.h"
//#include "subsongdlg.h"
#include <sdk/winamp/wa_ipc.h>
#include <sdk/winamp/ipc_pe.h>
#include <sdk/nu/autocharfn.h>
#include <sdk/nu/autowide.h>
#define SKIP_INT_DEFINES
#include <sdk/Agave/Language/api_language.h>
#include <loader/loader/utils.h>
#include "helpers.h"

#include <fcntl.h>
#include <stdio.h>
#include <io.h>
#include <string>
#include <vector>
#include <strsafe.h>

#define PLUGIN_VERSION L"2.1.5.10"

// TODO
// {5DF925A4-2095-460a-9394-155378C9D18B}
static const GUID InSidiousLangGUID = 
{ 0x5df925a4, 0x2095, 0x460a, { 0x93, 0x94, 0x15, 0x53, 0x78, 0xc9, 0xd1, 0x8b } };

// wasabi based services for localisation support
api_language *WASABI_API_LNG = 0;
HINSTANCE WASABI_API_LNG_HINST = 0, WASABI_API_ORIG_HINST = 0;

In_Module inmod;
CThreadSidPlayer *sidPlayer = NULL;
HANDLE gUpdaterThreadHandle = 0;
HANDLE gMutex = 0;

void GetFileExtensions(void);
DWORD WINAPI AddSubsongsThreadProc(void* params);

/** Structure for passing parameters to worker thread for adding subsong
*/
typedef struct tAddSubsongParams
{
	int numSubsongs;
	int foundIndex;
	int startSong;
	char fileName[512];
};

void config(HWND hwndParent)
{
	WASABI_API_DIALOGBOXPARAMW(IDD_CONFIG_DLG,hwndParent,&ConfigDlgWndProc,NULL);
}

void about(HWND hwndParent)
{
	wchar_t message[1024] = {0}, title[256] = {0};
	StringCchPrintf(message, 1024, WASABI_API_LNGSTRINGW(IDS_ABOUT_STRING),
					WASABI_API_LNGSTRINGW_BUF(IDS_PLUGIN_NAME, title, 256),
					PLUGIN_VERSION, TEXT(__DATE__));
	AboutMessageBox(hwndParent, message, title);
}

void createsidplayer()
{ 
	if (sidPlayer == NULL)
	{
		sidPlayer = new CThreadSidPlayer(inmod);

		if (sidPlayer != NULL)
		{
		sidPlayer->Init();

		gMutex = CreateMutex(NULL, FALSE, NULL);
	}
	}
}

int init() 
{ 
	WASABI_API_LNG = inmod.language;

	// need to have this initialised before we try to do anything with localisation features
	WASABI_API_START_LANG(inmod.hDllInstance, InSidiousLangGUID);

	// TODO localise
	inmod.description = (char*)(TEXT("SID Player v") PLUGIN_VERSION);
	return IN_INIT_SUCCESS;
}

void quit() { 
	/* one-time deinit, such as memory freeing */ 
	if (gMutex != NULL)
	{
		CloseHandle(gMutex);
		gMutex = NULL;
	}

	if (gUpdaterThreadHandle != NULL)
	{
		CloseHandle(gUpdaterThreadHandle);
		gUpdaterThreadHandle = NULL;
	}

	if (sidPlayer != NULL)
	{
		sidPlayer->Stop();
		delete sidPlayer;
		sidPlayer = NULL;
	}
}

int isourfile(const in_char *fn) { 
	return 0; 
} 

// called when winamp wants to play a file
int play(const in_char *fn) 
{ 
	std::string strFilename, str;

	createsidplayer();
	if(sidPlayer == NULL)
	{
		//error we cannot recover from
		return 1;
	}

	strFilename.assign(AutoCharFn(fn));

	int i = strFilename.find('}');
	if(i > 0) 
	{
		//assume char '{' will never occur in name unless its our subsong sign
		int j = strFilename.find('{');
		str = strFilename.substr(j+1,i -j -1);
		int subsongIndex = atoi(str.c_str());
		strFilename = strFilename.substr(i+1);
		sidPlayer->LoadTune(strFilename.c_str());
		sidPlayer->PlaySubtune(subsongIndex);
	}
	else 
	{
		sidPlayer->LoadTune(strFilename.c_str());
		const SidTuneInfo* tuneInfo = sidPlayer->GetTuneInfo();
		if (tuneInfo == NULL)
		{
			return -1;
		}
		sidPlayer->PlaySubtune(tuneInfo->startSong());
		// m_tune.getInfo()->startSong();
		//sidPlayer->PlaySubtune(subsongIndex);
	}

	return 0; 
}

// standard pause implementation
void pause() 
{
	if (sidPlayer != NULL)
	{
	sidPlayer->Pause();
}
}

void unpause() 
{
	if (sidPlayer != NULL)
	{
	sidPlayer->Play();
}
}

int ispaused() 
{ 
	return ((sidPlayer != NULL) ? ((sidPlayer->GetPlayerStatus() == SP_PAUSED) ? 1 : 0) : 0);
}

// stop playing.
void stop() 
{ 
	if (sidPlayer != NULL)
	{
	sidPlayer->Stop();
}
}

// returns length of playing track
int getlength() 
{
	//return (sidPlayer->GetNumSubtunes()-1)*1000;
	createsidplayer();
	return ((sidPlayer!= NULL) ? sidPlayer->GetSongLength()*1000 : 0);
}

// returns current output position, in ms.
// you could just use return mod.outMod->GetOutputTime(),
// but the dsp plug-ins that do tempo changing tend to make
// that wrong.
int getoutputtime() 
{ 
	//return sidPlayer->CurrentSubtune()*1000;
	//return inmod.outMod->GetOutputTime();
	return ((sidPlayer!= NULL) ? sidPlayer->GetPlayTime() : 0);
}

// called when the user releases the seek scroll bar.
// usually we use it to set seek_needed to the seek
// point (seek_needed is -1 when no seek is needed)
// and the decode thread checks seek_needed.
void setoutputtime(int time_in_ms) 
{ 
	if (sidPlayer != NULL)
	{
	//sidPlayer->PlaySubtune((time_in_ms / 1000)+1);
	sidPlayer->SeekTo(time_in_ms);
}
}

// standard volume/pan functions
void setvolume(int volume) { inmod.outMod->SetVolume(volume); }
void setpan(int pan) { inmod.outMod->SetPan(pan); }

// this gets called when the use hits Alt+3 to get the file info.
// if you need more info, ask me :)

int infoDlg(const in_char *fn, HWND hwnd)
{
	return INFOBOX_UNCHANGED;
}

/**
	Replaces occurences of string %{...} containing tokens separated by | by checking which
	token is empty. Example: %{sr|a} it means if %sr is empty then use %a (if artist from stil is empty use
	artist from SID file
*/
void conditionsReplace(std::string& formatString, const StilBlock* stilBlock, const SidTuneInfo* tuneInfo)
{
	const int BUF_SIZE = 30;
	std::string conditionToken;
	int tokenBeginPos = 0;
	vector<string> tokens;
	char toReplaceToken[BUF_SIZE];

	while ((tokenBeginPos = formatString.find("%{", tokenBeginPos)) >= 0)
	{
		int tokenEndPos = formatString.find('}', tokenBeginPos);
		if (tokenEndPos < 0)
		{
			break;
		}
		conditionToken = formatString.substr(tokenBeginPos + 2, tokenEndPos - tokenBeginPos - 2);
		StringCchPrintfA(toReplaceToken, 30, "%%{%s}", conditionToken.c_str());

		if (!conditionToken.empty())
		{
			tokens = split(conditionToken, '|');
			for (vector<string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
			{
				
				if ((*it).compare("f") == 0)
				{
					replaceAll(formatString, toReplaceToken, "%f");
					break;
				}
				if (((*it).compare("t") == 0)&&(strlen(tuneInfo->infoString(0)) > 0))
				{
					replaceAll(formatString, toReplaceToken, "%t");
					break;
				}
				if (((*it).compare("a") == 0) && (strlen(tuneInfo->infoString(1)) > 0))
				{
					replaceAll(formatString, toReplaceToken, "%a");
					break;
				}
				if (((*it).compare("r") == 0) && (strlen(tuneInfo->infoString(2)) > 0))
				{
					replaceAll(formatString, toReplaceToken, "%r");
					break;
				}
				if ((*it).compare("x") == 0)
				{
					replaceAll(formatString, toReplaceToken, "%x");
					break;
				}

				if (((*it).compare("sr") == 0) && (stilBlock != NULL) && (!stilBlock->ARTIST.empty()))
				{
					replaceAll(formatString, toReplaceToken, "%sr");
					break;
				}
				if (((*it).compare("st") == 0) && (stilBlock != NULL) && (!stilBlock->TITLE.empty()))
				{
					replaceAll(formatString, toReplaceToken, "%st");
					break;
				}
				if (((*it).compare("sa") == 0) && (stilBlock != NULL) && (!stilBlock->AUTHOR.empty()))
				{
					replaceAll(formatString, toReplaceToken, "%sa");
					break;
				}
				if (((*it).compare("sn") == 0) && (stilBlock != NULL) && (!stilBlock->NAME.empty()))
				{
					replaceAll(formatString, toReplaceToken, "%sn");
					break;
				}
			}
			//check if condition was replaced by token, if not then make final token empty
			if (conditionToken.at(0) != '%')
			{
				conditionToken.clear();
			}
		}

		++tokenBeginPos;
	}
}

const StilBlock *getStilBlock(const std::string &strFilename, const int subsongIndex)
{
	const StilBlock* sb = NULL;
	if ((sidPlayer != NULL) && (sidPlayer->GetCurrentConfig().useSTILfile == true))
	{
		sb = sidPlayer->GetSTILData2(strFilename.c_str(), subsongIndex - 1);
	}
	return sb;
}

// this is an odd function. it is used to get the title and/or
// length of a track.
// if filename is either NULL or of length 0, it means you should
// return the info of lastfn. Otherwise, return the information
// for the file in filename.
// if title is NULL, no title is copied into it.
// if length_in_ms is NULL, no length is copied into it.
void getfileinfo(const in_char *filename, in_char *title, int *length_in_ms)
{
	const SidTuneInfo* info = NULL;
	std::string str;
	std::string strFilename;
	int length;
	SidTune tune(0);
	int i;
	int foundindex = -1;
	int subsongIndex = 1;
	int plLength;
	wchar_t *plfilename;
	wchar_t* foundChar;
	bool firstSong = true;

	createsidplayer();

	if (gMutex != NULL)
	{
	WaitForSingleObject(gMutex, INFINITE);
	}

	if (gUpdaterThreadHandle != 0)
	{
		CloseHandle(gUpdaterThreadHandle);
		gUpdaterThreadHandle = 0;
	}

	if (!filename || !filename[0])
	{
		//get current song info
		if (sidPlayer != NULL)
		{
		info = sidPlayer->GetTuneInfo();
		}
		if (info == NULL)
		{
			ReleaseMutex(gMutex);
			return;
		}

		length = sidPlayer->GetSongLength();
		if (length == -1)
		{
			ReleaseMutex(gMutex);
			return;
		}

		//subsongIndex = info->currentSong();//.currentSong;
		strFilename.assign(info->path());
		strFilename.append(info->dataFileName());
		subsongIndex = sidPlayer->CurrentSubtune();
	}
	else
	{
		subsongIndex = 1;
		strFilename.assign(AutoCharFn(filename));
		if(strFilename[0] == '{') 
		{
			firstSong = false;
			//assume char '{' will never occur in name unless its our subsong sign
			i = strFilename.find('}');
			str = strFilename.substr(1,i -1);
			subsongIndex = atoi(str.c_str());
			strFilename = strFilename.substr(i+1);
			//get info from other file if we got real name
			tune.load(strFilename.c_str());
		}
		else
		{
			tune.load(strFilename.c_str());
			info = tune.getInfo();
			if (info == NULL)
			{
				ReleaseMutex(gMutex);
				return;
			}

			subsongIndex = tune.getInfo()->startSong();
		}

		info = tune.getInfo();
		//tune.selectSong(info.startSong);
		tune.selectSong(subsongIndex);
		length = sidPlayer->GetSongLength(tune);
		if (length == -1)
		{
			ReleaseMutex(gMutex);
			return;
		}
	}
	
	//check if we got correct tune info
	//if (info.c64dataLen == 0) return;
	if (info->c64dataLen() == 0)
	{
		ReleaseMutex(gMutex);
		return;
	}

	length *= 1000;
	if (length <= 0)
	{
		length = -1000;
	}

	if (length_in_ms != NULL)
	{
		*length_in_ms = length;
	}
	
	/* build file title from template:
	%f - filename
	%t - song title from sid file
	%a - artist
	%r - release year and publisher
	%x - subsong string
	
	%n - subsong number in subsong string

	%sr - artist from STIL file
	%st - title from STIL file
	%sa - author from STIL file
	%sn - name from STIL file
	*/
	//info->dataFileName

	std::string fileNameOnly(strFilename);
	int cutStart = fileNameOnly.find_last_of("\\");
	int cutEnd = fileNameOnly.find_last_of(".");
	fileNameOnly = fileNameOnly.substr(cutStart + 1, cutEnd - cutStart-1);


	//std::string titleTemplate("%f / %a / %x %sn");
	std::string titleTemplate("%t %x / %a / %r / %sn");
	std::string subsongTemplate("%n");


	//fill STIL data if necessary
	const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
	conditionsReplace(titleTemplate, sb, info);

	replaceAll(titleTemplate, "%f", fileNameOnly.c_str());
	replaceAll(titleTemplate, "%t", info->infoString(0));
	replaceAll(titleTemplate, "%a", info->infoString(1));
	replaceAll(titleTemplate, "%r", info->infoString(2));
	if (info->songs() > 1) 
	{
		char buf[20] = {0};
		StringCchPrintfA(buf,20,"%02d", subsongIndex);
		replaceAll(subsongTemplate, "%n", buf);
		replaceAll(titleTemplate, "%x", subsongTemplate.c_str());
	}
	else
	{
		replaceAll(titleTemplate, "%x", "");
	}

	//fill STIL data if necessary
	if (sb == NULL)
	{
		replaceAll(titleTemplate, "%sr", "");
		replaceAll(titleTemplate, "%sa", "");
		replaceAll(titleTemplate, "%st", "");
		replaceAll(titleTemplate, "%sn", "");
	}
	else
	{			
		replaceAll(titleTemplate, "%sr", sb->ARTIST.c_str());
		replaceAll(titleTemplate, "%st", sb->TITLE.c_str());
		replaceAll(titleTemplate, "%sa", sb->AUTHOR.c_str());
		replaceAll(titleTemplate, "%sn", sb->NAME.c_str());
	}

	if (title != NULL)
	{
		lstrcpyn(title, AutoWide(titleTemplate.c_str()), GETFILEINFO_TITLE_LENGTH);
	}

	if ((info->songs() == 1) || (firstSong == false) || !filename || !filename[0])
	{
		ReleaseMutex(gMutex);
		return;
	}

	//we have subsongs...
	plLength = (int)SendMessage(inmod.hMainWindow,WM_WA_IPC,0,IPC_GETLISTLENGTH);
	//check if we have already added subsongs
	for (i=0; i<plLength;++i)
	{
		plfilename = (wchar_t*)SendMessage(inmod.hMainWindow,WM_WA_IPC,i,IPC_GETPLAYLISTFILEW);
		if (!plfilename || (plfilename[0] != '{')) continue;
		foundChar = wcschr(plfilename,L'}');
		if (_wcsicmp(foundChar+1,filename) == 0)
		{
			//subtunes were added no point to do it again
			ReleaseMutex(gMutex);
			return;
		}
	}	


	//first get entry index after which we will add subsong entry
	for(i=0; i<plLength;++i)
	{
		plfilename = (wchar_t*)SendMessage(inmod.hMainWindow,WM_WA_IPC,i,IPC_GETPLAYLISTFILEW);
		if (!plfilename) continue;
		if (_wcsicmp(plfilename,filename) == 0)
		{
			foundindex = i;
			break;
		}
	}	

	//run another thread for adding subsongs
	tAddSubsongParams *threadParams = new tAddSubsongParams();
	threadParams->foundIndex = foundindex;
	threadParams->numSubsongs = info->songs();
	threadParams->startSong = info->startSong();
	strncpy(threadParams->fileName, AutoCharFn(filename), ARRAYSIZE(threadParams->fileName));
	//gUpdaterThreadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AddSubsongsThreadProc, (void*)threadParams, 0, NULL);
	AddSubsongsThreadProc((void*)threadParams);

	ReleaseMutex(gMutex);
	return;
}

DWORD WINAPI AddSubsongsThreadProc(void* params)
{
	/*if (gMutex != NULL)
	{
	WaitForSingleObject(gMutex, INFINITE);
	}*/

	tAddSubsongParams* threadParams = reinterpret_cast<tAddSubsongParams*>(params);
	fileinfo *fi = new fileinfo;
	if (fi)
	{
	COPYDATASTRUCT *cds = new COPYDATASTRUCT;
		if (cds)
		{
	std::string strFilename;
	char buf[20];
	int foundindex = threadParams->foundIndex;
	//get HWND of playlist window
			HWND h = GetPlaylistWnd();//(HWND)SendMessage(inmod.hMainWindow, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND);
	for (int i = 1; i <= threadParams->numSubsongs; ++i)
	{
		//first entry in playlist will be the startSong that is why we don't add it
		if (i == threadParams->startSong)
		{
			continue;
		}

		++foundindex;
				StringCchPrintfA(buf, ARRAYSIZE(buf), "{%d}", i);
		strFilename.assign(buf);
		strFilename.append(threadParams->fileName);
		ZeroMemory(fi, sizeof(fileinfo));
		strncpy(fi->file, strFilename.c_str(), MAX_PATH);
		fi->index = foundindex;
		ZeroMemory(cds, sizeof(COPYDATASTRUCT));
		cds->dwData = IPC_PE_INSERTFILENAME;
		cds->lpData = fi;
		cds->cbData = sizeof(fileinfo);
		SendMessage(h, WM_COPYDATA, 0, (LPARAM)cds);
	}

	delete cds;
		}
		delete fi;
	}
	delete threadParams;

	//ReleaseMutex(gMutex);
	return 0;
}

// module definition.
extern In_Module inmod = 
{
	IN_VER_WACUP,	// defined in IN2.H
	(char *)L"SID Player v" PLUGIN_VERSION,
	0,	// hMainWindow (filled in by winamp)
	0,  // hDllInstance (filled in by winamp)
	NULL,	// filled in later
	1,	// is_seekable
	1,	// uses output plug-in system
	config,
	about,
	init,
	quit,
	getfileinfo,
	infoDlg,
	isourfile,
	play,
	pause,
	unpause,
	ispaused,
	stop,
	
	getlength,
	getoutputtime,
	setoutputtime,

	setvolume,
	setpan,

	0,0,0,0,0,0,0,0,0, // visualization calls filled in by winamp

	0,0, // dsp calls filled in by winamp

	NULL,

	NULL,		// setinfo call filled in by winamp

	0,			// out_mod filled in by winamp
	NULL,       // api_service
	GetFileExtensions	// loading optimisation
};

void GetFileExtensions(void)
{
	static bool loaded_extensions;
	if (!loaded_extensions)
	{
		// TODO localise
		inmod.FileExtensions = (char*)L"SID\0Commodore 64 SID Music File (*.SID)\0";
		loaded_extensions = true;
	}
}

extern "C" __declspec(dllexport) In_Module* winampGetInModule2()
{
   return &inmod;
}

extern "C" __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	// TODO
	// prompt to remove our settings with default as no (just incase)
	/*if (MessageBox( hwndDlg, WASABI_API_LNGSTRINGW( IDS_UNINSTALL_SETTINGS_PROMPT ),
				    pluginTitle, MB_YESNO | MB_DEFBUTTON2 ) == IDYES ) {
		SaveNativeIniString(PLUGIN_INI, CONFIG_APP_NAME, 0, 0);
	}*/

	// as we're not hooking anything and have no settings we can support an on-the-fly uninstall action
	return IN_PLUGIN_UNINSTALL_NOW;
}

// return 1 if you want winamp to show it's own file info dialogue, 0 if you want to show your own (via In_Module.InfoBox)
// if returning 1, remember to implement winampGetExtendedFileInfo("formatinformation")!
extern "C" __declspec(dllexport) int winampUseUnifiedFileInfoDlg(const wchar_t * fn)
{
	return 1;
}

// should return a child window of 513x271 pixels (341x164 in msvc dlg units), or return NULL for no tab.
// Fill in name (a buffer of namelen characters), this is the title of the tab (defaults to "Advanced").
// filename will be valid for the life of your window. n is the tab number. This function will first be 
// called with n == 0, then n == 1 and so on until you return NULL (so you can add as many tabs as you like).
// The window you return will recieve WM_COMMAND, IDOK/IDCANCEL messages when the user clicks OK or Cancel.
// when the user edits a field which is duplicated in another pane, do a SendMessage(GetParent(hwnd),WM_USER,(WPARAM)L"fieldname",(LPARAM)L"newvalue");
// this will be broadcast to all panes (including yours) as a WM_USER.
extern "C" __declspec(dllexport) HWND winampAddUnifiedFileInfoPane(int n, const wchar_t * filename,
																   HWND parent, wchar_t *name, size_t namelen)
{
	if (n == 0)
	{
		// add first pane
		SetPropW(parent, L"INBUILT_NOWRITEINFO", (HANDLE)1);

		const SidTuneInfo* info;
		SidTune tune(0);
		int i;
		std::string strfilename;

		strfilename.assign(AutoCharFn(filename));
		i = strfilename.find('}');
		if (i > 0) 
		{
			strfilename = strfilename.substr(i+1);
		}
		tune.load(strfilename.c_str());
		info = tune.getInfo();
		if (info)
		{
			// TODO localise
			wcsncpy(name, L"STIL Information", namelen);
			return WASABI_API_CREATEDIALOGPARAMW(IDD_INFO, parent, InfoDlgWndProc, (LPARAM)info);
		}
	}
	return NULL;
}

extern "C" __declspec (dllexport) int winampGetExtendedFileInfoW(wchar_t *filename, char *metadata, wchar_t *ret, int retlen)
{
	int retval = 0;

	if (!_stricmp(metadata, "type"))
	{
		ret[0] = '0';
		ret[1] = 0;
		return 1;
	}
	else if (!_stricmp(metadata, "family") ||
			 // TODO add more to "formatinformation" ?
			 !_stricmp(metadata, "formatinformation"))
	{
		if (!filename || !filename[0])
		{
			return 0;
		}

		lstrcpyn(ret, L"Commodore 64 Music File", retlen);
		return 1;
	}

	if (!filename || !*filename)
	{
		return 0;
	}

	createsidplayer();

	const SidTuneInfo* info = NULL;
	std::string str;
	std::string strFilename;
	int length;
	SidTune tune(0);
	int subsongIndex = 1;
	//bool firstSong = true;

	if (gMutex != NULL)
	{
	WaitForSingleObject(gMutex, INFINITE);
	}

	if (gUpdaterThreadHandle != 0)
	{
		CloseHandle(gUpdaterThreadHandle);
		gUpdaterThreadHandle = 0;
	}

	if (!filename || !filename[0])
	{
		//get current song info
		if (sidPlayer != NULL)
		{
		info = sidPlayer->GetTuneInfo();
		}
		if (info == NULL)
		{
			ReleaseMutex(gMutex);
			return 0;
		}

		length = sidPlayer->GetSongLength();
		if (length == -1)
		{
			ReleaseMutex(gMutex);
			return 0;
		}

		//subsongIndex = info->currentSong();//.currentSong;
		strFilename.assign(info->path());
		strFilename.append(info->dataFileName());
		subsongIndex = sidPlayer->CurrentSubtune();
	}
	else
	{
		subsongIndex = 1;
		strFilename.assign(AutoCharFn(filename));
		if (strFilename[0] == '{') 
		{
			//firstSong = false;
			//assume char '{' will never occur in name unless its our subsong sign
			int i = strFilename.find('}');
			str = strFilename.substr(1,i -1);
			subsongIndex = atoi(str.c_str());
			strFilename = strFilename.substr(i+1);
			//get info from other file if we got real name
			tune.load(strFilename.c_str());
		}
		else
		{
			tune.load(strFilename.c_str());
			info = tune.getInfo();
			if (info == NULL)
			{
				ReleaseMutex(gMutex);
				return 0;
			}

			subsongIndex = tune.getInfo()->startSong();
		}

		info = tune.getInfo();
		//tune.selectSong(info.startSong);
		tune.selectSong(subsongIndex);
		length = sidPlayer->GetSongLength(tune);
		if (length == -1)
		{
			ReleaseMutex(gMutex);
			return 0;
		}
	}
	
	//check if we got correct tune info
	//if (info.c64dataLen == 0) return;
	if (!info || info->c64dataLen() == 0)
	{
		ReleaseMutex(gMutex);
		return 0;
	}

	// even if no file, return a 1 and write "0"
	if (!_stricmp(metadata, "length"))
	{
		length *= 1000;
		if (length <= 0)
		{
			length = -1000;
		}

		_itow_s(length, ret, retlen, 10);
		retval = 1;
	}
	else if (!_stricmp(metadata, "title"))
	{
		const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
		if (sb != NULL)
		{
			if (!sb->TITLE.empty())
			{
				StringCchPrintf(ret, retlen, L"%S", sb->TITLE.c_str());
				return 1;
			}
		}

		StringCchPrintf(ret, retlen, L"%S", info->infoString(0));
		retval = 1;
	}
	else if (!_stricmp(metadata, "artist"))
	{
		const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
		if (sb != NULL)
		{
			if (!sb->ARTIST.empty())
			{
				StringCchPrintf(ret, retlen, L"%S", sb->ARTIST.c_str());
				return 1;
			}
		}

		StringCchPrintf(ret, retlen, L"%S", info->infoString(1));
		retval = 1;
	}
	else if (!_stricmp(metadata, "album"))
	{
		const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
		if (sb != NULL)
		{
			if (!sb->NAME.empty())
			{
				StringCchPrintf(ret, retlen, L"%S", sb->NAME.c_str());
				return 1;
			}
		}
	}
	else if (!_stricmp(metadata, "comment"))
	{
		const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
		if (sb != NULL)
		{
			if (!sb->COMMENT.empty())
			{
				StringCchPrintf(ret, retlen, L"%S", sb->COMMENT.c_str());
				return 1;
			}
		}

		StringCchPrintf(ret, retlen, L"%S", info->commentString(0));
		retval = 1;
	}
	else if (!_stricmp(metadata, "publisher"))
	{
		const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
		if (sb != NULL)
		{
			if (!sb->AUTHOR.empty())
			{
				StringCchPrintf(ret, retlen, L"%S", sb->AUTHOR.c_str());
				return 1;
			}
		}

		// same string is used for both so we skip the
		// first part to get to the publisher details
		char *pub_str = (char *)info->infoString(2);
		if (pub_str && *pub_str)
		{
			while (pub_str && *pub_str != ' ')
			{
				++pub_str;
			}

			if (pub_str && *pub_str)
			{
				if (*pub_str == ' ')
				{
					++pub_str;
				}

				StringCchPrintf(ret, retlen, L"%S", pub_str);
				retval = 1;
			}
		}
	}
	else if (!_stricmp(metadata, "year"))
	{
		// same string is used for both so we ignore
		// most of it & just attempt to get a number
		char *year_str = (char *)info->infoString(2);
		if (year_str && *year_str)
		{
			// copy up to the space into the buffer
			int count = 0;
			while (year_str && *year_str != ' ' && count < retlen)
			{
				ret[count] = *year_str;
				++count;
				++year_str;
			}

			retval = 1;
		}
	}
	else if (!_stricmp(metadata, "track"))
	{
		if (info->songs() > 1) 
		{
			_itow_s(subsongIndex, ret, retlen, 10);
		}
		else
		{
			wcsncpy(ret, L"1", retlen);
		}
		retval = 1;
	}

	return retval;
}

/* *********************************** */
/* placeholder for conversion api support */

extern "C" __declspec(dllexport) intptr_t winampGetExtendedRead_openW(const wchar_t *fn, int *size, int *bps, int *nch, int *srate)
{
	CThreadSidDecoder *sidDecoder = new CThreadSidDecoder();
	if (sidDecoder)
	{
		// TODO
		return (intptr_t)sidDecoder;
	}
	return 0;
}

extern "C" __declspec(dllexport) size_t winampGetExtendedRead_getData(intptr_t handle, char *dest, size_t len, int *killswitch)
{
	CThreadSidDecoder *sidDecoder = (CThreadSidDecoder *)handle;
	if (sidDecoder)
	{
		// TODO
	return 0;
}
	return -1;
}

extern "C" __declspec(dllexport) int winampGetExtendedRead_setTime(intptr_t handle, int millisecs)
{
	CThreadSidDecoder *sidDecoder = (CThreadSidDecoder *)handle;
	if (sidDecoder)
	{
		// TODO
		return 1;
	}
	return 0;
}

extern "C" __declspec(dllexport) void winampGetExtendedRead_close(intptr_t handle)
{
	CThreadSidDecoder *sidDecoder = (CThreadSidDecoder *)handle;
	if (sidDecoder)
	{
		sidDecoder->Stop();
		delete sidDecoder;
	}
}