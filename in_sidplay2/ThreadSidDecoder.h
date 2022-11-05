#pragma once

#include <shlobj.h>
#include <fstream>
#include <stdlib.h>
#include <map>
#include <vector>

#include "residfp.h"
#include "sidplayfp/sidplayfp.h"
#include "sidplayfp/SidTuneInfo.h"
#include "sidplayfp/SidTune.h"
#include "utils/SidDatabase.h"

#include "typesdefs.h"

#define PLAYBACK_BIT_PRECISION 16

using namespace std;

class CThreadSidDecoder
{
private:
    sidplayfp *m_engine;    
	SidTune m_tune;
	PlayerConfig m_playerConfig;
	HANDLE m_threadHandle;
	PlayerStatus_t m_playerStatus;
	unsigned __int64 m_decodedSampleCount;
	unsigned __int64 m_playTimems; //int 
	char* m_decodeBuf;
	int m_decodeBufLen;
	int m_currentTuneLengthMs;
private:
	static DWORD __stdcall Run(void* thisparam);
	void AssignConfigValue(PlayerConfig *conf, string token, string value);
	SidDatabase m_sidDatabase;
	void ReadLine(char* buf,FILE *file,const int maxBuf);
protected:
	int m_seekNeedMs;	
protected:
	void DoSeek();
public:	
	CThreadSidDecoder(void);
	~CThreadSidDecoder(void);
	void Init(void);
	void Play(void);
	void Stop(void);
	void LoadTune(const char* name);
	PlayerStatus_t GetPlayerStatus() { return m_playerStatus;}
	int CurrentSubtune(void);
	void PlaySubtune(int subTune);
	const SidTuneInfo* GetTuneInfo(void);
	bool LoadConfigFromFile(PlayerConfig *conf);
	void SetConfig(PlayerConfig* newConfig);
	int GetSongLengthMs(void);
	//! Moves emulation time pointer to given time
	void SeekTo(int timeMs);
};
