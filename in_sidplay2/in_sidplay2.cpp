#define PLUGIN_VERSION L"2.5.0.4"
#define PLUGIN_LIBRARY_BUILD_DATE L"2.5.0 - 2 Jun 2023"

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
#include <sdk/nu/autowide.h>
#define SKIP_INT_DEFINES
#include <sdk/Agave/Language/api_language.h>
#include <loader/loader/paths.h>
#include <loader/loader/utils.h>
#include "helpers.h"

#include <fcntl.h>
#include <stdio.h>
#include <io.h>
#include <string>
#include <vector>
#include <strsafe.h>

// TODO
// {5DF925A4-2095-460a-9394-155378C9D18B}
static const GUID InSidiousLangGUID = 
{ 0x5df925a4, 0x2095, 0x460a, { 0x93, 0x94, 0x15, 0x53, 0x78, 0xc9, 0xd1, 0x8b } };

// wasabi based services for localisation support
SETUP_API_LNG_VARS;

static prefsDlgRecW* preferences;

In_Module plugin;
CRITICAL_SECTION g_sidPlayer_cs = { 0 };
CThreadSidPlayer *sidPlayer = NULL;
//HANDLE gUpdaterThreadHandle = 0;
//HANDLE gMutex = 0;

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
	OpenPrefsPage((WPARAM)preferences);
}

void about(HWND hwndParent)
{
	wchar_t message[1024] = { 0 }, title[256] = { 0 };
	StringCchPrintf(message, ARRAYSIZE(message), WASABI_API_LNGSTRINGW(IDS_ABOUT_STRING),
					WASABI_API_LNGSTRINGW_BUF(IDS_PLUGIN_NAME, title, ARRAYSIZE(title)),
							 PLUGIN_VERSION, TEXT(__DATE__), PLUGIN_LIBRARY_BUILD_DATE);
	AboutMessageBox(hwndParent, message, title);
}

void createsidplayer(void)
{
	EnterCriticalSection(&g_sidPlayer_cs);

	if (sidPlayer == NULL)
	{
		sidPlayer = new CThreadSidPlayer(plugin);

		if (sidPlayer != NULL)
		{
			sidPlayer->Init();

			//gMutex = CreateMutex(NULL, FALSE, NULL);
		}
	}

	LeaveCriticalSection(&g_sidPlayer_cs);
}

int init(void)
{ 
	WASABI_API_LNG = plugin.language;

	// need to have this initialised before we try to do anything with localisation features
	WASABI_API_START_LANG(plugin.hDllInstance, InSidiousLangGUID);

	// TODO localise
	plugin.description = (char*)(TEXT("SID Player v") PLUGIN_VERSION);

	preferences = (prefsDlgRecW*)GlobalAlloc(GPTR, sizeof(prefsDlgRecW));
	if (preferences)
	{
		preferences->hInst = plugin.hDllInstance;
		preferences->dlgID = IDD_CONFIG_DLG;
		preferences->name = WASABI_API_LNGSTRINGW_DUP(IDS_SID);
		preferences->proc = ConfigDlgWndProc;
		preferences->where = 10;
		preferences->_id = 98;
		preferences->next = (_prefsDlgRec*)0xACE;
		AddPrefsPage((WPARAM)preferences, TRUE);
	}

	InitializeCriticalSectionEx(&g_sidPlayer_cs, 400, CRITICAL_SECTION_NO_DEBUG_INFO);
	return IN_INIT_SUCCESS;
}

void quit(void) {
	/* one-time deinit, such as memory freeing */ 
	/*if (gMutex != NULL)
	{
		CloseHandle(gMutex);
		gMutex = NULL;
	}*/

	/*if (gUpdaterThreadHandle != NULL)
	{
		CloseHandle(gUpdaterThreadHandle);
		gUpdaterThreadHandle = NULL;
	}*/

	EnterCriticalSection(&g_sidPlayer_cs);

	if (sidPlayer != NULL)
	{
		sidPlayer->Stop();
		delete sidPlayer;
		sidPlayer = NULL;
	}

	LeaveCriticalSection(&g_sidPlayer_cs);

	DeleteCriticalSection(&g_sidPlayer_cs);
}

/*int isourfile(const in_char *fn) { 
	return 0; 
}*/

// called when winamp wants to play a file
int play(const in_char *filename) 
{ 
	std::string strFilename, str;

	createsidplayer();
	if(sidPlayer == NULL)
	{
		//error we cannot recover from
		return 1;
	}

	LPCSTR fn = ConvertPathToA(filename, NULL, 0, CP_ACP);
	strFilename = fn;
	AutoCharDupFree((void*)fn);

	const size_t i = strFilename.find('}');
	if(i > 0) 
	{
		//assume char '{' will never occur in name unless its our subsong sign
		const size_t j = strFilename.find('{');
		str = strFilename.substr(j+1,i -j -1);
		int subsongIndex = AStr2I(str.c_str());
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
	}

	return 0; 
}

// standard pause implementation
void pause(void)
{
	if (sidPlayer != NULL)
	{
		sidPlayer->Pause();
	}
}

void unpause(void)
{
	if (sidPlayer != NULL)
	{
		sidPlayer->Play();
	}
}

int ispaused(void)
{ 
	return ((sidPlayer != NULL) ? ((sidPlayer->GetPlayerStatus() == SP_PAUSED) ? 1 : 0) : 0);
}

// stop playing.
void stop(void)
{
	if (sidPlayer != NULL)
	{
		sidPlayer->Stop();
	}
}

// returns length of playing track
int getlength(void)
{
	//return (sidPlayer->GetNumSubtunes()-1)*1000;
	createsidplayer();
	return ((sidPlayer!= NULL) ? sidPlayer->GetSongLengthMs() : 0);
}

// returns current output position, in ms.
// you could just use return mod.outMod->GetOutputTime(),
// but the dsp plug-ins that do tempo changing tend to make
// that wrong.
int getoutputtime(void)
{ 
	//return sidPlayer->CurrentSubtune()*1000;
	//return plugin.outMod->GetOutputTime();
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
void setvolume(int volume) { plugin.outMod->SetVolume(volume); }
void setpan(int pan) { plugin.outMod->SetPan(pan); }

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

	while ((tokenBeginPos = (int)formatString.find("%{", tokenBeginPos)) >= 0)
	{
		const int tokenEndPos = (int)formatString.find('}', tokenBeginPos);
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

extern "C" __declspec(dllexport) int GetSubSongInfo(const wchar_t *filename)
{
	createsidplayer();

	/*if (gMutex != NULL)
	{
		WaitForSingleObject(gMutex, INFINITE);
	}*/

	SidTune tune(0);
	LPCSTR fn = ConvertPathToA(filename, NULL, 0, CP_ACP);
	tune.load(fn);
	AutoCharDupFree((void*)fn);

	const SidTuneInfo* tuneInfo = tune.getInfo();
	if (tuneInfo == NULL)
	{
		//ReleaseMutex(gMutex);
		return 0;
	}

	const int ret = tuneInfo->songs();
	//ReleaseMutex(gMutex);
	return ret;
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
	const SidTuneInfo* tuneInfo = NULL;
	std::string str;
	std::string strFilename;
	int length;
	SidTune tune(0);
	int foundindex = -1;
	int subsongIndex = 1;
	int plLength;
	wchar_t *plfilename;
	wchar_t* foundChar;
	bool firstSong = true;

	createsidplayer();

	/*if (gMutex != NULL)
	{
		WaitForSingleObject(gMutex, INFINITE);
	}*/

	/*if (gUpdaterThreadHandle != 0)
	{
		CloseHandle(gUpdaterThreadHandle);
		gUpdaterThreadHandle = 0;
	}*/

	if (!filename || !filename[0])
	{
		//get current song info
		if (sidPlayer != NULL)
		{
			tuneInfo = sidPlayer->GetTuneInfo();
		}
		if (tuneInfo == NULL)
		{
			//ReleaseMutex(gMutex);
			return;
		}

		length = sidPlayer->GetSongLengthMs();
		if (length < 0)
		{
			//ReleaseMutex(gMutex);
			return;
		}

		//subsongIndex = info->currentSong();//.currentSong;
		strFilename.assign(tuneInfo->path());
		strFilename.append(tuneInfo->dataFileName());
		subsongIndex = sidPlayer->CurrentSubtune();
	}
	else
	{
		subsongIndex = 1;

		LPCSTR fn = ConvertPathToA(filename, NULL, 0, CP_ACP);
		strFilename = fn;
		AutoCharDupFree((void*)fn);

		if(strFilename[0] == '{') 
		{
			firstSong = false;
			//assume char '{' will never occur in name unless its our subsong sign
			const size_t i = strFilename.find('}');
			str = strFilename.substr(1, i -1);
			subsongIndex = AStr2I(str.c_str());
			strFilename = strFilename.substr(i + 1);
			//get info from other file if we got real name
			tune.load(strFilename.c_str());
			tuneInfo = tune.getInfo();
			if (tuneInfo == NULL)
			{
				//ReleaseMutex(gMutex);
				return;
			}
		}
		else
		{
			tune.load(strFilename.c_str());
			tuneInfo = tune.getInfo();
			if (tuneInfo == NULL)
			{
				//ReleaseMutex(gMutex);
				return;
			}

			subsongIndex = tuneInfo->startSong();
		}

		//tune.selectSong(info.startSong);
		tune.selectSong(subsongIndex);
		length = sidPlayer->GetSongLengthMs(tune);
		if (length < 0)
		{
			//ReleaseMutex(gMutex);
			return;
		}
	}
	
	//check if we got correct tune info
	//if (info.c64dataLen == 0) return;
	if (tuneInfo->c64dataLen() == 0)
	{
		//ReleaseMutex(gMutex);
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
	const size_t cutStart = fileNameOnly.find_last_of("\\"),
				 cutEnd = fileNameOnly.find_last_of(".");
	fileNameOnly = fileNameOnly.substr(cutStart + 1, cutEnd - cutStart-1);


	//std::string titleTemplate("%f / %a / %x %sn");
	std::string titleTemplate("%t %x / %a / %r / %sn");
	std::string subsongTemplate("%n");


	//fill STIL data if necessary
	const StilBlock* sb = getStilBlock(strFilename, subsongIndex);
	conditionsReplace(titleTemplate, sb, tuneInfo);

	replaceAll(titleTemplate, "%f", fileNameOnly.c_str());
	replaceAll(titleTemplate, "%t", tuneInfo->infoString(0));
	replaceAll(titleTemplate, "%a", tuneInfo->infoString(1));
	replaceAll(titleTemplate, "%r", tuneInfo->infoString(2));
	if (tuneInfo->songs() > 1)
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

	//if ((info->songs() == 1) || (firstSong == false) || !filename || !filename[0])
	{
		//ReleaseMutex(gMutex);
		return;
	}

	//we have subsongs...
	/*plLength = (int)GetPlaylistLength();
	//check if we have already added subsongs
	for (i=0; i<plLength;++i)
	{
		plfilename = (wchar_t*)SendMessage(plugin.hMainWindow,WM_WA_IPC,i,IPC_GETPLAYLISTFILEW);
		if (!plfilename || (plfilename[0] != '{')) continue;
		foundChar = wcschr(plfilename,L'}');
		if (SameStr(foundChar+1,filename))
		{
			//subtunes were added no point to do it again
			ReleaseMutex(gMutex);
			return;
		}
	}	


	//first get entry index after which we will add subsong entry
	for(i=0; i<plLength;++i)
	{
		plfilename = (wchar_t*)SendMessage(plugin.hMainWindow,WM_WA_IPC,i,IPC_GETPLAYLISTFILEW);
		if (!plfilename) continue;
		if (SameStr(plfilename,filename))
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
	//gUpdaterThreadHandle = CreateThread(NULL, 0, AddSubsongsThreadProc, (void*)threadParams, 0, NULL);
	AddSubsongsThreadProc((void*)threadParams);

	ReleaseMutex(gMutex);
	return;*/
}

#if 0
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
			HWND h = GetPlaylistWnd();//(HWND)SendMessage(plugin.hMainWindow, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND);
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
#endif

// module definition.
extern In_Module plugin = 
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
	0/*isourfile*/,
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
	INPUT_HAS_READ_META | INPUT_USES_UNIFIED_ALT3 |
	INPUT_ADDS_TAB_TO_UNIFIED_ALT3 |
	INPUT_HAS_FORMAT_CONVERSION_UNICODE |
	INPUT_HAS_FORMAT_CONVERSION_SET_TIME_MODE,
	GetFileExtensions,	// loading optimisation
	IN_INIT_WACUP_END_STRUCT
};

void GetFileExtensions(void)
{
	static bool loaded_extensions;
	if (!loaded_extensions)
	{
		// TODO localise
		plugin.FileExtensions = (char*)L"SID\0Commodore 64 SID Music File (*.SID)\0";
		loaded_extensions = true;
	}
}

extern "C" __declspec(dllexport) In_Module* winampGetInModule2(void)
{
   return &plugin;
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
		const SidTuneInfo* info;
		SidTune tune(0);
		std::string strfilename;

		LPCSTR fn = ConvertPathToA(filename, NULL, 0, CP_ACP);
		strfilename = fn;
		AutoCharDupFree((void*)fn);

		const size_t i = strfilename.find('}');
		if (i > 0) 
		{
			strfilename = strfilename.substr(i+1);
		}
		tune.load(strfilename.c_str());
		info = tune.getInfo();
		if (info)
		{
			SetProp(parent, L"INBUILT_NOWRITEINFO", (HANDLE)1);

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

	if (!_stricmp(metadata, "type") ||
		!_stricmp(metadata, "streammetadata"))
	{
		ret[0] = '0';
		ret[1] = 0;
		return 1;
	}
    else if (!_stricmp(metadata, "streamgenre") ||
			 !_stricmp(metadata, "streamtype") ||
             !_stricmp(metadata, "streamurl") ||
             !_stricmp(metadata, "streamname"))
    {
        return 0;
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

	const SidTuneInfo* tuneInfo = NULL;
	std::string str;
	std::string strFilename;
	int length;
	SidTune tune(0);
	int subsongIndex = 1;
	//bool firstSong = true;

	/*if (gMutex != NULL)
	{
		WaitForSingleObject(gMutex, INFINITE);
	}*/

	/*if (gUpdaterThreadHandle != 0)
	{
		CloseHandle(gUpdaterThreadHandle);
		gUpdaterThreadHandle = 0;
	}*/

	if (!filename || !filename[0])
	{
		//get current song info
		if (sidPlayer != NULL)
		{
			tuneInfo = sidPlayer->GetTuneInfo();
		}
		if (tuneInfo == NULL)
		{
			//ReleaseMutex(gMutex);
			return 0;
		}

		length = sidPlayer->GetSongLengthMs();
		if (length < 0)
		{
			//ReleaseMutex(gMutex);
			return 0;
		}

		//subsongIndex = info->currentSong();//.currentSong;
		strFilename.assign(tuneInfo->path());
		strFilename.append(tuneInfo->dataFileName());
		subsongIndex = sidPlayer->CurrentSubtune();
	}
	else
	{
		subsongIndex = 1;

		LPCSTR fn = ConvertPathToA(filename, NULL, 0, CP_ACP);
		strFilename = fn;
		AutoCharDupFree((void*)fn);

		if (strFilename[0] == '{') 
		{
			//firstSong = false;
			//assume char '{' will never occur in name unless its our subsong sign
			const size_t i = strFilename.find('}');
			str = strFilename.substr(1,i -1);
			subsongIndex = AStr2I(str.c_str());
			strFilename = strFilename.substr(i+1);
			//get info from other file if we got real name
			tune.load(strFilename.c_str());
			tuneInfo = tune.getInfo();
			if (tuneInfo == NULL)
			{
				//ReleaseMutex(gMutex);
				return 0;
			}
		}
		else
		{
			tune.load(strFilename.c_str());
			tuneInfo = tune.getInfo();
			if (tuneInfo == NULL)
			{
				//ReleaseMutex(gMutex);
				return 0;
			}

			subsongIndex = tuneInfo->startSong();
		}

		//tune.selectSong(info.startSong);
		tune.selectSong(subsongIndex);
		length = sidPlayer->GetSongLengthMs(tune);
		if (length < 0)
		{
			//ReleaseMutex(gMutex);
			return 0;
		}
	}
	
	//check if we got correct tune info
	//if (info.c64dataLen == 0) return;
	if (!tuneInfo || tuneInfo->c64dataLen() == 0)
	{
		//ReleaseMutex(gMutex);
		return 0;
	}

	// even if no file, return a 1 and write "0"
	if (!_stricmp(metadata, "length"))
	{
		if (length <= 0)
		{
			length = -1000;
		}

		I2WStr(length, ret, retlen);
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

		StringCchPrintf(ret, retlen, L"%S", tuneInfo->infoString(0));
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

		StringCchPrintf(ret, retlen, L"%S", tuneInfo->infoString(1));
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

		StringCchPrintf(ret, retlen, L"%S", tuneInfo->commentString(0));
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
		char *pub_str = (char *)tuneInfo->infoString(2);
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
		char *year_str = (char *)tuneInfo->infoString(2);
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
		if (tuneInfo->songs() > 1)
		{
			I2WStr(subsongIndex, ret, retlen);
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
#if 0
	CThreadSidDecoder *sidDecoder = new CThreadSidDecoder();
	if (sidDecoder)
	{
		std::string strFilename, str;
		strFilename.assign(AutoCharFn(fn));

		int i = strFilename.find('}');
		if (i > 0)
		{
			//assume char '{' will never occur in name unless its our subsong sign
			int j = strFilename.find('{');
			str = strFilename.substr(j + 1, i - j - 1);
			int subsongIndex = AStr2I(str.c_str());
			strFilename = strFilename.substr(i + 1);
			//sidDecoder->LoadTune(strFilename.c_str());
			//sidDecoder->PlaySubtune(subsongIndex);
		}
		//else
		//{
			sidDecoder->LoadTune(strFilename.c_str());
			const SidTuneInfo* tuneInfo = sidDecoder->GetTuneInfo();
			if (tuneInfo == NULL)
			{
				delete sidDecoder;
				return 0;
			}
			//sidDecoder->PlaySubtune(tuneInfo->startSong());
		//}

		if (bps)
		{
			*bps = 16/*dec->GetBitsperSample()*/;
		}
		if (nch)
		{
			*nch = 2/*dec->GetNumberofChannel()*/;
		}
		if (srate)
		{
			*srate = 44100/*dec->GetSampleRate()*/;
		}
		if (size)
		{
			*size = -1/*sidDecoder->GetSongLength() * (*bps / 8) * (*nch)*/;
		}

		// TODO
		return (intptr_t)sidDecoder;
	}
#endif
	return 0;
}

extern "C" __declspec(dllexport) intptr_t winampGetExtendedRead_getData(intptr_t handle, char *dest, size_t len, int *killswitch)
{
#if 0
	CThreadSidDecoder *sidDecoder = (CThreadSidDecoder *)handle;
	if (sidDecoder)
	{
		// TODO
		return 0;
	}
#endif
	return -1;
}

extern "C" __declspec(dllexport) int winampGetExtendedRead_setTime(intptr_t handle, int millisecs)
{
#if 0
	CThreadSidDecoder *sidDecoder = (CThreadSidDecoder *)handle;
	if (sidDecoder)
	{
		// TODO
		return 1;
	}
#endif
	return 0;
}

extern "C" __declspec(dllexport) void winampGetExtendedRead_close(intptr_t handle)
{
#if 0
	CThreadSidDecoder *sidDecoder = (CThreadSidDecoder *)handle;
	if (sidDecoder)
	{
		sidDecoder->Stop();
		delete sidDecoder;
	}
#endif
}