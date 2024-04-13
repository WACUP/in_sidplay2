#include <windows.h>
#include <shlwapi.h>
#include "ThreadSidDecoder.h"
#include "SidInfoImpl.h"
#include "c64roms.h"
#include <loader/loader/paths.h>
#define WA_UTILS_SIMPLE
#include <loader/loader/utils.h>

CThreadSidDecoder::CThreadSidDecoder(void): m_tune(0), m_threadHandle(0)
{
	m_decodeBuf = NULL;
	m_decodeBufLen = 0;
	m_playerStatus = SP_STOPPED;
	m_playerConfig.playLimitEnabled = false;
	m_playerConfig.playLimitSec = 120;
	m_playerConfig.songLengthsFile = NULL;
	m_playerConfig.useSongLengthFile = false;
	m_playerConfig.useSTILfile = false;
	m_playerConfig.hvscDirectory = NULL;
	m_playerConfig.voiceConfig[0][0] = true;
	m_playerConfig.voiceConfig[0][1] = true;
	m_playerConfig.voiceConfig[0][2] = true;
	m_playerConfig.voiceConfig[1][0] = true;
	m_playerConfig.voiceConfig[1][1] = true;
	m_playerConfig.voiceConfig[1][2] = true;
	m_playerConfig.voiceConfig[2][0] = true;
	m_playerConfig.voiceConfig[2][1] = true;
	m_playerConfig.voiceConfig[2][2] = true;

	m_playerConfig.pseudoStereo = false;
	m_playerConfig.sid2Model = SidConfig::sid_model_t::MOS6581;	
	m_currentTuneLengthMs = -1000;
	m_seekNeedMs = 0;
	m_engine = new sidplayfp;
}

CThreadSidDecoder::~CThreadSidDecoder(void)
{
	if(m_decodeBufLen > 0) delete[] m_decodeBuf;

	if (m_engine != NULL)
	{
		if (m_playerConfig.sidConfig.sidEmulation != NULL)
		{
			delete m_playerConfig.sidConfig.sidEmulation;
			m_playerConfig.sidConfig.sidEmulation = NULL;
		}
		delete m_engine;
	}
	//it = m.begin();
//	m_sidDatabase.close();
}

void CThreadSidDecoder::Init(void)
{ 
	if(m_playerStatus != SP_STOPPED) Stop();

	m_playerConfig.sidConfig = m_engine->config();
	//m_playerConfig.sidConfig.sampleFormat = SID2_LITTLE_SIGNED;	

	if(!LoadConfigFromFile(&m_playerConfig))
	{
		//if load fails then use this default settings
		//m_playerConfig.sidConfig.precision = 16;
		SidConfig *defaultConf = new SidConfig;
		memcpy((void*)&(m_playerConfig.sidConfig), defaultConf, sizeof(SidConfig));
		
		delete defaultConf;
		m_playerConfig.sidConfig.frequency = 44100;
		m_playerConfig.sidConfig.playback = SidConfig::MONO;// sid2_mono;
	}
	//m_playerConfig.sidConfig.sampleFormat = SID2_LITTLE_SIGNED;	

	SetConfig(&m_playerConfig);
}

void CThreadSidDecoder::Play(void)
{
	if(m_playerStatus == SP_RUNNING) return;
	if(m_playerStatus == SP_PAUSED) 
	{
		m_playerStatus = SP_RUNNING;
		ResumeThread(m_threadHandle);
		return;	
	}
	//if stopped then create new thread to play
	if(m_playerStatus == SP_STOPPED)
	{
		/*int numChann = (m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1;
		m_inmod->SetInfo((m_playerConfig.sidConfig.frequency * PLAYBACK_BIT_PRECISION * numChann)/1000, m_playerConfig.sidConfig.frequency /1000,numChann,1);*/

		m_playerStatus = SP_RUNNING;

		// TODO api_config & thread priority level
		m_threadHandle = StartThread(CThreadSidDecoder::Run, this,
								THREAD_PRIORITY_HIGHEST, 0, NULL);
	}
}

void CThreadSidDecoder::Stop(void)
{
	if(m_playerStatus == SP_STOPPED) return;

	const bool paused = (m_playerStatus == SP_PAUSED);
	m_playerStatus = SP_STOPPED; //to powinno zatrzymaæ w¹tek
	if (paused)
	{
		ResumeThread(m_threadHandle);
	}

	if(WaitForSingleObject(m_threadHandle,3000) == WAIT_TIMEOUT)
	{
		TerminateThread(m_threadHandle,0);
	}
	m_engine->stop();
	CloseHandle(m_threadHandle);
	m_threadHandle = NULL;
}

void CThreadSidDecoder::LoadTune(const char* name)
{
	Stop();

	m_tune.load(name);
	const SidTuneInfo* tuneInfo = m_tune.getInfo();
	if (tuneInfo == NULL)
	{
		return;
	}
	m_tune.selectSong(tuneInfo->startSong());

	m_currentTuneLengthMs = m_sidDatabase.lengthMs(m_tune);
	if ((m_playerConfig.playLimitEnabled) && (m_currentTuneLengthMs <= 0))
	{
		m_currentTuneLengthMs = (m_playerConfig.playLimitSec * 1000);
	}
	m_engine->load(&m_tune);
	//mute must be applied after SID's have been created
	for (int sid = 0; sid < 3; ++sid)
	{
		for (int voice = 0; voice < 3; ++voice)
		{
			m_engine->mute(sid, voice, !m_playerConfig.voiceConfig[sid][voice]);
		}
	}
}

DWORD WINAPI CThreadSidDecoder::Run(void* thisparam)
{
#if 0
	int desiredLen;
	int decodedLen;
	int numChn;
	int bps;
	int dspDataLen = 0;
	int freq;
	CThreadSidDecoder *playerObj = static_cast<CThreadSidDecoder*>(thisparam);

	playerObj->m_decodedSampleCount = 0;
	playerObj->m_playTimems = 0;
	bps = PLAYBACK_BIT_PRECISION;//playerObj->m_playerConfig.sidConfig.precision;
	numChn = (playerObj->m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1;
	freq = playerObj->m_playerConfig.sidConfig.frequency;
	desiredLen = 576 * (PLAYBACK_BIT_PRECISION >>3) * numChn * (playerObj->m_inmod->dsp_isactive()?2:1);

	while(playerObj->m_playerStatus != SP_STOPPED)
	{
		if(playerObj->m_inmod->outMod->CanWrite() >= desiredLen)
		{
			//decode music data from libsidplay object
			//pierwotnie libsidplay operowa³ na bajtach i wszystkie d³ugoœci bufora by³y w bajtach
			//libsidplayfp operuje na samplach 16 bitowych wiêc musimy odpowiednio mni¿yæ lub dzieliæ przez 2 liczbê bajtów
			decodedLen = 2 * playerObj->m_engine->play(reinterpret_cast<short*>(playerObj->m_decodeBuf),desiredLen / 2);
			//playerObj->m_decodedSampleCount += decodedLen / numChn / (bps>>3);
			//write it to vis subsystem
			playerObj->m_inmod->SAAddPCMData(playerObj->m_decodeBuf,numChn,bps,playerObj->m_playTimems);
			playerObj->m_inmod->VSAAddPCMData(playerObj->m_decodeBuf,numChn,bps,playerObj->m_playTimems);

			playerObj->m_decodedSampleCount += decodedLen / numChn / (bps>>3);
			playerObj->m_playTimems =(playerObj->m_decodedSampleCount * 1000) / playerObj->m_playerConfig.sidConfig.frequency;
			//use DSP plugin on data
			if(playerObj->m_inmod->dsp_isactive())
			{
				decodedLen = playerObj->m_inmod->dsp_dosamples(reinterpret_cast<short*>(playerObj->m_decodeBuf),
					decodedLen / numChn / (bps>>3),bps,numChn,freq);
				decodedLen *= (numChn * (bps>>3));
			}
			playerObj->m_inmod->outMod->Write(playerObj->m_decodeBuf,decodedLen);
		}
		else
		{
			//do we need to seek ??
			if(playerObj->m_seekNeedMs > 0) 
				playerObj->DoSeek();
			/*else
				Sleep(20);*/
		}

		//int timeElapsed = playerObj->GetPlayTime();
		//int timeElapsed = playerObj->m_inmod->outMod->GetOutputTime();
		int timeElapsed = playerObj->m_playTimems;
		//if we konw the song length and timer just reached it then go to next song
		
		const int lengthMs = playerObj->GetSongLengthMs();
		if(lengthMs >= 1)
		{
			if(lengthMs < timeElapsed)
			{
				playerObj->m_playerStatus = SP_STOPPED;
				PostEOF();
				return 0;
			}
			//Sleep(10);
		}
		else //if we dont know song length but time limit is enabled then check it
			if(playerObj->m_playerConfig.playLimitEnabled) 
			{
				if((playerObj->m_playerConfig.playLimitSec*1000) < timeElapsed) 
				{
					playerObj->m_playerStatus = SP_STOPPED;
					PostEOF();
					//Sleep(10);
					return 0;
				}
			}
		//no song length, and no length limit so play for infinity
	}
#endif
	return 0;
}

int CThreadSidDecoder::CurrentSubtune(void)
{
	if(m_tune.getStatus())
	{
		const SidTuneInfo* tuneInfo = m_tune.getInfo();
		return (tuneInfo ? tuneInfo->currentSong() : 0);
	}
	return 0;
}

void CThreadSidDecoder::PlaySubtune(int subTune)
{	
	Stop();
	m_tune.selectSong(subTune);	
	m_currentTuneLengthMs = m_sidDatabase.lengthMs(m_tune);
	if ((m_playerConfig.playLimitEnabled) && (m_currentTuneLengthMs <= 0))
	{
		m_currentTuneLengthMs = (m_playerConfig.playLimitSec * 1000);
	}
	m_engine->stop();
	m_engine->load(&m_tune);
	Play();
}

const SidTuneInfo* CThreadSidDecoder::GetTuneInfo(void)
{
	return (m_tune.getStatus()) ? m_tune.getInfo() : NULL; //SidTuneInfo();
}

bool CThreadSidDecoder::LoadConfigFromFile(PlayerConfig *conf)
{
	if(conf == NULL) return false;

	static wchar_t fileName[MAX_PATH];
	char cLine[200+MAX_PATH];
	int maxLen = 200+MAX_PATH;
	string sLine; 
	string token;
	string value;
	FILE *cfgFile;

	if (!fileName[0])
	{
		// use the settings path so we can have a portable wacup install no matter what :)
		CombinePath(fileName, GetPaths()->settings_sub_dir, L"in_sidplay2.ini");
	}

	cfgFile = _wfopen(fileName,L"rb");

	if(cfgFile == NULL) return false;
	while(feof(cfgFile) == 0)
	{
		//file>>cLine; 
		ReadLine(cLine,cfgFile,maxLen);
		if(strlen(cLine) == 0) continue;
		sLine.assign(cLine);
		const size_t pos = sLine.find("=");
		token = sLine.substr(0,pos);
		value = sLine.substr(pos+1);
		if((token.length() ==0) || (value.length() ==0)) continue;
		while((value.at(0) == '\"') && (value.at(value.length()-1) != '\"') && (!feof(cfgFile)))
		{
			ReadLine(cLine,cfgFile,maxLen);
			sLine.append(cLine);
		}
		if((value.at(0) == '\"') && (value.at(value.length()-1) == '\"'))
			value = value.substr(1,value.length() -2);
		AssignConfigValue(conf, token, value);
	}
	fclose(cfgFile);
	return true;
}

void CThreadSidDecoder::ReadLine(char* buf,FILE *file,const int maxBuf)
{
	char c;
	int pos =0;

	do
	{
		size_t readCount = fread(&c,1,1,file);
		if(readCount ==0) break;
		if((c != '\r') && (c != '\n')) buf[pos++]=c;
		if(pos == maxBuf) break;
	}
	while(c != '\n');
	buf[pos]='\0';
}

void CThreadSidDecoder::AssignConfigValue(PlayerConfig* plconf,string token, string value)
{
	SidConfig* conf = &plconf->sidConfig;
	if(token.compare("PlayFrequency") == 0) { conf->frequency = AStr2I(value.c_str()); return; }
	if(token.compare("PlayChannels") == 0) 
	{
		if(value.compare("1") == 0)
		{
			conf->playback = SidConfig::MONO;
		}
		else
		{
			conf->playback = SidConfig::STEREO;
		}
		return;
	}
	if(token.compare("C64Model") == 0) 
	{
		conf->defaultC64Model = (SidConfig::c64_model_t)AStr2I(value.c_str());
		return;
	}
	if (token.compare("C64ModelForced") == 0)
	{
		conf->forceC64Model = (bool)AStr2I(value.c_str());
		return;
	}

	if(token.compare("SidModel") == 0) 
	{
		conf->defaultSidModel = (SidConfig::sid_model_t)AStr2I(value.c_str());
		return;
	}

	if (token.compare("VoiceConfig") == 0)
	{
		int digitId = 0;
		for (int sid = 0; sid < 3; ++sid)
		{
			for (int voice = 0; voice < 3; ++voice)
			{
				plconf->voiceConfig[sid][voice] = (value.at(digitId++) == '1');
			}
		}
		return;
	}

	if (token.compare("Sid2Model") == 0)
	{
		plconf->sid2Model = (SidConfig::sid_model_t)AStr2I(value.c_str());
		return;
	}

	if (token.compare("PseudoStereo") == 0)
	{
		plconf->pseudoStereo = (bool)AStr2I(value.c_str());
		return;
	}

	if (token.compare("SidModelForced") == 0)
	{
		conf->forceSidModel = (bool)AStr2I(value.c_str());
		return;
	}

	/*if (token.compare("Sid2ModelForced") == 0)
	{
		conf->forceSecondSidModel = (bool)AStr2I(value.c_str());
		return;
	}*/

	if(token.compare("PlayLimitEnabled") == 0) 
	{
		plconf->playLimitEnabled = (bool)AStr2I(value.c_str());
		return;
	}
	if(token.compare("PlayLimitTime") == 0) 
	{
		plconf->playLimitSec = AStr2I(value.c_str());
		return;
	}

	if(token.compare("UseSongLengthFile") == 0)
	{
		plconf->useSongLengthFile =(bool)AStr2I(value.c_str());
		return;
	}
	if(token.compare("SongLengthsFile") == 0)
	{
		plconf->songLengthsFile = new char[value.length()+1];

		strcpy(plconf->songLengthsFile,value.c_str());
		return;
	}

	if(token.compare("HVSCDir") == 0)
	{
		plconf->hvscDirectory = new char[value.length()+1];
		strcpy(plconf->hvscDirectory,value.c_str());
		return;
	}
	if(token.compare("UseSTILFile") == 0)
	{
		plconf->useSTILfile =(bool)AStr2I(value.c_str());
		return;
	}
}

void CThreadSidDecoder::SetConfig(PlayerConfig* newConfig)
{
	int numChann;

	if(m_playerStatus != SP_STOPPED) Stop();
	m_engine->stop();

	sidbuilder* currentBuilder = m_playerConfig.sidConfig.sidEmulation;
	if (m_playerConfig.sidConfig.sidEmulation != NULL)
	{
		//delete m_playerConfig.sidConfig.sidEmulation;
	}
	m_playerConfig.sidConfig.sidEmulation = 0;
	m_engine->config(m_playerConfig.sidConfig);
	if (currentBuilder != NULL)
	{
		delete currentBuilder;
	}

	//change assign to memcpy !
	m_playerConfig.sidConfig.frequency = newConfig->sidConfig.frequency;
	m_playerConfig.sidConfig.playback = newConfig->sidConfig.playback;
	m_playerConfig.sidConfig.defaultC64Model = newConfig->sidConfig.defaultC64Model;
	m_playerConfig.sidConfig.forceC64Model = newConfig->sidConfig.forceC64Model;
	m_playerConfig.sidConfig.defaultSidModel = newConfig->sidConfig.defaultSidModel;
	m_playerConfig.sidConfig.forceSidModel = newConfig->sidConfig.forceSidModel;
	m_playerConfig.sidConfig.forceSecondSidModel = newConfig->sidConfig.forceSecondSidModel;
	m_playerConfig.sidConfig.secondSidModel = newConfig->sidConfig.secondSidModel;
	


	m_playerConfig.playLimitEnabled = newConfig->playLimitEnabled;
	m_playerConfig.playLimitSec = newConfig->playLimitSec;
	m_playerConfig.useSongLengthFile = newConfig->useSongLengthFile;

	m_playerConfig.sidConfig.samplingMethod = SidConfig::INTERPOLATE; //RESAMPLE_INTERPOLATE

	for (int sid = 0; sid < 3; ++sid)
	{
		for (int voice = 0; voice < 3; ++voice)
		{
			m_playerConfig.voiceConfig[sid][voice] = newConfig->voiceConfig[sid][voice];
		}
	}
	m_playerConfig.pseudoStereo = newConfig->pseudoStereo;
	m_playerConfig.sid2Model = newConfig->sid2Model;
	

	
	//TODO czy trzeba drugi i trzeci adres sida??????

	//string memory cannot overlap !!!
	if(m_playerConfig.songLengthsFile != newConfig->songLengthsFile)
	{
		if(m_playerConfig.songLengthsFile != NULL) 
		{
			delete[] m_playerConfig.songLengthsFile;
			m_playerConfig.songLengthsFile = NULL;
		}
		if(newConfig->songLengthsFile != NULL)
		{
			m_playerConfig.songLengthsFile = new char[strlen(newConfig->songLengthsFile)+1];
			strcpy(m_playerConfig.songLengthsFile, newConfig->songLengthsFile);
		}
	}

	m_playerConfig.useSTILfile = newConfig->useSTILfile;
	if(m_playerConfig.hvscDirectory != newConfig->hvscDirectory)
	{
		if(m_playerConfig.hvscDirectory != NULL) 
		{
			delete[] m_playerConfig.hvscDirectory;
			m_playerConfig.hvscDirectory = NULL;
		}
		if(newConfig->hvscDirectory != NULL)
		{
			m_playerConfig.hvscDirectory = new char[strlen(newConfig->hvscDirectory)+1];
			strcpy(m_playerConfig.hvscDirectory, newConfig->hvscDirectory);
		}
	}

	//m_sidBuilder = ReSIDBuilderCreate("");
    //SidLazyIPtr<IReSIDBuilder> rs(m_sidBuilder);

	ReSIDfpBuilder* rs = new ReSIDfpBuilder("ReSIDfp");
    if (rs)
    {		
		//const SidInfoImpl* si = reinterpret_cast<const SidInfoImpl*>(&m_engine->info());

		m_playerConfig.sidConfig.sidEmulation = rs;
		rs->create((m_engine->info()).maxsids());

		rs->filter6581Curve(0.5);
		rs->filter8580Curve((double)12500);
		//filter always enabled
		rs->filter(true);		
	}

	//TO CHANGE !!!!!!!
	if (m_playerConfig.pseudoStereo)
	{
		m_playerConfig.sidConfig.secondSidAddress = 0xD400;
		m_playerConfig.sidConfig.secondSidModel = m_playerConfig.sid2Model;
	}
	else
	{
		m_playerConfig.sidConfig.secondSidAddress = 0;
		m_playerConfig.sidConfig.secondSidModel = -1;
	}
	//m_playerConfig.sidConfig.
	m_engine->config(m_playerConfig.sidConfig);


	//kernal,basic,chargen
	m_engine->setRoms(KERNAL_ROM, BASIC_ROM, CHARGEN_ROM);

	//create decode buf for 576 samples
	if(m_decodeBufLen > 0) delete[] m_decodeBuf;
	numChann = (m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1;
	m_decodeBufLen = 2 * 576 * (PLAYBACK_BIT_PRECISION >>3) * numChann;
	m_decodeBuf = new char[m_decodeBufLen];
	//open song length database
	if((m_playerConfig.useSongLengthFile) && (m_playerConfig.songLengthsFile != NULL))
	{
		/*bool openRes = */m_sidDatabase.open(m_playerConfig.songLengthsFile);
		//if(openRes != 0) MessageBoxA(NULL,"Error opening songlength database.\r\nDisable songlength databse or choose other file","in_sidplay2",MB_OK);
	}
}

int CThreadSidDecoder::GetSongLengthMs(void)
{
	return m_currentTuneLengthMs;
}

void CThreadSidDecoder::DoSeek()
{
	int bits;
	int skip_bytes;
	int numChn = (m_playerConfig.sidConfig.playback == SidConfig::STEREO) ? 2 : 1;
	int freq = m_playerConfig.sidConfig.frequency;
	int timesek = m_seekNeedMs / 1000;
	if (timesek == 0) return;

	if (m_seekNeedMs <= m_playTimems)
	{
		timesek = m_seekNeedMs / 1000;
		if (timesek == 0) return;
		//seek time is less than current time - we have to rewind song

		const SidTuneInfo* tuneInfo = m_tune.getInfo();
		if (tuneInfo == NULL) return;

		m_tune.selectSong(tuneInfo->currentSong());
		//m_currentTuneLength = m_sidDatabase.length(m_tune);//we know length of tune already
		m_engine->stop();
		m_engine->load(&m_tune);//timers are now 0
	}
	else
	{
		timesek = (m_seekNeedMs - m_playTimems) / 1000;
		if (timesek <= 0) return;
	}

	bits = PLAYBACK_BIT_PRECISION;//m_playerConfig.sidConfig.precision;
	m_engine->fastForward(3200);
	skip_bytes = (timesek * freq * numChn * (bits >> 3)) >> 5;
	//m_decodedSampleCount += skip_bytes / numChn / (bps>>3); //not needed
	while (skip_bytes > m_decodeBufLen)
	{
		int decodedLen = 2 * m_engine->play(reinterpret_cast<short*>(m_decodeBuf), m_decodeBufLen / 2);
		skip_bytes -= decodedLen;
	}
	/*
	if (skip_bytes >= 16)
	{
		//int decodedLen = 2 * m_engine->play(reinterpret_cast<short*>(m_decodeBuf), skip_bytes / 2);
	}
	*/
	//now take time calculationns from emulation engine and calculate other variables

	m_engine->time();
	m_playTimems = (m_engine->time() * 1000);// / timer->timebase();
	m_decodedSampleCount = (m_playTimems * freq) / 1000;
	//m_playTimems =(m_decodedSampleCount * 1000) / m_playerConfig.sidConfig.frequency;
	m_engine->fastForward(100);
	m_seekNeedMs = 0;
}


void CThreadSidDecoder::SeekTo(int timeMs)
{
	m_seekNeedMs = timeMs;
}
