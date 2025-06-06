#include <windows.h>
#include <shlwapi.h>
#include "ThreadSidPlayer.h"
#include "SidInfoImpl.h"
#include "c64roms.h"
#include <loader/loader/paths.h>
#include <loader/loader/utils.h>
#include <Agave/Config/api_config.h>

CThreadSidPlayer::CThreadSidPlayer(In_Module& inWAmod): m_tune(0), m_threadHandle(0)
{
	m_inmod = &inWAmod;
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
	maxLatency =0;
	m_seekNeedMs = 0;
	m_engine = new sidplayfp;
}

CThreadSidPlayer::~CThreadSidPlayer(void)
{
	if(m_decodeBufLen > 0) delete[] m_decodeBuf;
	ClearSTILData();
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

void CThreadSidPlayer::Init(void)
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

void CThreadSidPlayer::Play(void)
{
	if(m_playerStatus == SP_RUNNING) return;
	if(m_playerStatus == SP_PAUSED) 
	{
		m_playerStatus = SP_RUNNING;
		if (m_inmod->outMod)
		{
			m_inmod->outMod->Pause(0);
		}
		ResumeThread(m_threadHandle);
		return;	
	}
	//if stopped then create new thread to play
	if(m_playerStatus == SP_STOPPED)
	{
		const int numChann = (!m_inmod->config->GetBool(playbackConfigGroupGUID, L"mono", false) ? 
							  (m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1 : 1);

		// for the playback state to match correctly with the wacup core playback
		// config then we have to nudge everything otherwise stereo vs mono modes
		// may not be correctly applied at the time that it's expected to be done
		const SidConfig::playback_t playback = m_playerConfig.sidConfig.playback;
		m_playerConfig.sidConfig.playback = (SidConfig::playback_t)numChann;
		m_engine->config(m_playerConfig.sidConfig);
		m_playerConfig.sidConfig.playback = playback;

		maxLatency = (m_inmod->outMod && m_inmod->outMod->Open &&
					  m_playerConfig.sidConfig.frequency && numChann ?
					  m_inmod->outMod->Open(m_playerConfig.sidConfig.frequency,
					  numChann, PLAYBACK_BIT_PRECISION, -1, -1) : -1);
		if (maxLatency < 0)
		{
			return;
		}

		//visualization init
		m_inmod->SAVSAInit(maxLatency,m_playerConfig.sidConfig.frequency);
		m_inmod->VSASetInfo(m_playerConfig.sidConfig.frequency,numChann);

		m_inmod->SetInfo((m_playerConfig.sidConfig.frequency * PLAYBACK_BIT_PRECISION * numChann) / 1000,
						  m_playerConfig.sidConfig.frequency / 1000, numChann, 1);

		//default volume
		m_inmod->outMod->SetVolume(-666); 
		m_playerStatus = SP_RUNNING;
		m_threadHandle = StartThread(CThreadSidPlayer::Run, this, static_cast<int>(m_inmod->
									 config->GetInt(playbackConfigGroupGUID, L"priority",
														THREAD_PRIORITY_HIGHEST)), 0, NULL);
	}
}

void CThreadSidPlayer::Pause(void)
{
	if(m_playerStatus == SP_RUNNING)
	{
		SuspendThread(m_threadHandle);
		if (m_inmod->outMod)
		{
			m_inmod->outMod->Pause(1);
		}
		m_playerStatus = SP_PAUSED;
	}
}

void CThreadSidPlayer::Stop(void)
{
	if(m_playerStatus == SP_STOPPED) return;

	const bool paused = (m_playerStatus == SP_PAUSED);
	m_playerStatus = SP_STOPPED; //to powinno zatrzyma� w�tek
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
	// close output system

	if(m_inmod->outMod != NULL && m_inmod->outMod->Close != NULL) m_inmod->outMod->Close();
	// deinitialize visualization
	if (m_inmod->outMod) m_inmod->SAVSADeInit();
}

void CThreadSidPlayer::LoadTune(const char* name)
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

DWORD WINAPI CThreadSidPlayer::Run(void* thisparam)
{
	int desiredLen;
	int decodedLen;
	int numChn;
	int bps;
	int dspDataLen = 0;
	int freq;
	CThreadSidPlayer *playerObj = static_cast<CThreadSidPlayer*>(thisparam);

	playerObj->m_decodedSampleCount = 0;
	playerObj->m_playTimems = 0;
	bps = PLAYBACK_BIT_PRECISION;//playerObj->m_playerConfig.sidConfig.precision;
	numChn = (!playerObj->m_inmod->config->GetBool(playbackConfigGroupGUID, L"mono", false) ?
			  (playerObj->m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1 : 1);
	freq = playerObj->m_playerConfig.sidConfig.frequency;
	desiredLen = 576 * (PLAYBACK_BIT_PRECISION >>3) * numChn * (playerObj->m_inmod->dsp_isactive()?2:1);

	while(playerObj->m_playerStatus != SP_STOPPED)
	{
		if(playerObj->m_inmod->outMod->CanWrite() >= desiredLen)
		{
			//decode music data from libsidplay object
			//pierwotnie libsidplay operowa� na bajtach i wszystkie d�ugo�ci bufora by�y w bajtach
			//libsidplayfp operuje na samplach 16 bitowych wi�c musimy odpowiednio mni�y� lub dzieli� przez 2 liczb� bajt�w
			decodedLen = 2 * playerObj->m_engine->play(reinterpret_cast<short*>(playerObj->m_decodeBuf),desiredLen / 2);
			//playerObj->m_decodedSampleCount += decodedLen / numChn / (bps>>3);
			//write it to vis subsystem
			playerObj->m_inmod->SAAddPCMData(playerObj->m_decodeBuf,numChn,bps,(int)playerObj->m_playTimems);
			/*playerObj->m_inmod->VSAAddPCMData(playerObj->m_decodeBuf,numChn,bps,playerObj->m_playTimems);*/

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
			else
				SleepEx(10, TRUE);
		}

		//int timeElapsed = playerObj->GetPlayTime();
		//int timeElapsed = playerObj->m_inmod->outMod->GetOutputTime();
		const int timeElapsed = (const int)playerObj->m_playTimems;
		//if we konw the song length and timer just reached it then go to next song
		
		const int length = playerObj->GetSongLengthMs();
		if(length >= 1)
		{
			if(length < timeElapsed)
			{
				playerObj->m_playerStatus = SP_STOPPED;

				playerObj->m_inmod->outMod->Write(NULL, 0);
				playerObj->m_inmod->SAVSADeInit();

				while (playerObj->m_inmod->outMod->IsPlaying())
				{
					SleepEx(10, TRUE);
				}

				PostEOF();
				return 0;
			}
		}
		else //if we dont know song length but time limit is enabled then check it
			if(playerObj->m_playerConfig.playLimitEnabled) 
			{
				if((playerObj->m_playerConfig.playLimitSec*1000) < timeElapsed) 
				{
					playerObj->m_playerStatus = SP_STOPPED;

					playerObj->m_inmod->outMod->Write(NULL, 0);
					playerObj->m_inmod->SAVSADeInit();

					while (playerObj->m_inmod->outMod->IsPlaying())
					{
						SleepEx(10, TRUE);
					}

					PostEOF();
					return 0;
				}
			}
		//no song length, and no length limit so play for infinity
	}

	return 0;
}

int CThreadSidPlayer::CurrentSubtune(void)
{
	if(m_tune.getStatus())
	{
		const SidTuneInfo* tuneInfo = m_tune.getInfo();
		return (tuneInfo ? tuneInfo->currentSong() : 0);
	}
	return 0;
}

void CThreadSidPlayer::PlaySubtune(int subTune)
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

const SidTuneInfo* CThreadSidPlayer::GetTuneInfo(void)
{
	return (m_tune.getStatus()) ? m_tune.getInfo() : NULL; //SidTuneInfo();
}

int CThreadSidPlayer::GetPlayTime(void)
{
	return (int)(m_inmod->outMod ? m_playTimems+(m_inmod->outMod->GetOutputTime()-m_inmod->outMod->GetWrittenTime()) : 0);
	//return ((m_timer->time()*1000)/m_timer->timebase()) + (m_inmod->outMod->GetOutputTime()-m_inmod->outMod->GetWrittenTime()); 
}

bool CThreadSidPlayer::LoadConfigFromFile(PlayerConfig *conf)
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

void CThreadSidPlayer::ReadLine(char* buf,FILE *file,const int maxBuf)
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

void CThreadSidPlayer::SaveConfigToFile(PlayerConfig *plconf)
{
	if(plconf != NULL)
	{
		static wchar_t fileName[MAX_PATH];
		if (!fileName[0])
		{
			// use the settings path so we can have a portable wacup install no matter what :)
			CombinePath(fileName, GetPaths()->settings_sub_dir, L"in_sidplay2.ini");
		}

		SidConfig* conf = &plconf->sidConfig;
		ofstream outFile(fileName);
		if (conf->frequency != 44100)
		{
			outFile << "PlayFrequency=" << conf->frequency << endl;
		}

		if (conf->playback == SidConfig::STEREO)
		{
			outFile << "PlayChannels=2" << endl;
		}

		if (conf->defaultC64Model)
		{
			outFile << "C64Model=" << conf->defaultC64Model << endl;
		}


		if (conf->forceC64Model)
		{
			outFile << "C64ModelForced=" << conf->forceC64Model << endl;
		}

		if (conf->defaultSidModel)
		{
			outFile << "SidModel=" << conf->defaultSidModel << endl;
		}

		if (conf->forceSidModel)
		{
			outFile << "SidModelForced=" << conf->forceSidModel << endl;
		}

		//outFile << "Sid2ModelForced=" << conf->forceSecondSidModel << endl;

		if (plconf->playLimitEnabled)
		{
			outFile << "PlayLimitEnabled=" << plconf->playLimitEnabled << endl;
		}

		if (plconf->playLimitSec != 120)
		{
			outFile << "PlayLimitTime=" << plconf->playLimitSec << endl;
		}

		if (plconf->useSongLengthFile)
		{
			outFile << "UseSongLengthFile=" << plconf->useSongLengthFile << endl;
		}

		if ((!plconf->useSongLengthFile) || (plconf->songLengthsFile == NULL))
		{
			//outFile << "SongLengthsFile=" << "" << endl;
		}
		else
		{
			outFile << "SongLengthsFile=" << plconf->songLengthsFile << endl;
		}

		if (plconf->useSTILfile)
		{
			outFile << "UseSTILFile=" << plconf->useSTILfile << endl;
		}

		if (plconf->hvscDirectory == NULL)
		{
			plconf->useSTILfile = false;
		}

		if (!plconf->useSTILfile)
		{
			//outFile << "HVSCDir=" << "" << endl;
		}
		else
		{
			outFile << "HVSCDir=" << plconf->hvscDirectory << endl;
		}

		if (plconf->useSongLengthFile)
		{
			outFile << "UseSongLengthFile=" << plconf->useSongLengthFile << endl;
		}

		outFile << "VoiceConfig=";
		for (int sid = 0; sid < 3; ++sid)
		{
			for (int voice = 0; voice < 3; ++voice)
			{
				outFile << plconf->voiceConfig[sid][voice];
			}
		}
		outFile << endl;

		if (plconf->pseudoStereo)
		{
			outFile << "PseudoStereo=" << plconf->pseudoStereo << endl;
		}

		if (plconf->sid2Model != SidConfig::sid_model_t::MOS6581)
		{
			outFile << "Sid2Model=" << plconf->sid2Model << endl;
		}

		outFile.close();
	}
}

void CThreadSidPlayer::AssignConfigValue(PlayerConfig* plconf,string token, string value)
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

const PlayerConfig& CThreadSidPlayer::GetCurrentConfig()
{
	return m_playerConfig;
	/*
	PlayerConfig cfgcpy;
	memcpy(&cfgcpy,&m_playerConfig,sizeof(PlayerConfig));
	cfgcpy.sidConfig.sidEmulation = NULL;
	if(m_playerConfig.songLengthsFile != NULL)
	{
		cfgcpy.songLengthsFile = new char[strlen(m_playerConfig.songLengthsFile)+1];
		strcpy(cfgcpy.songLengthsFile,m_playerConfig.songLengthsFile);
	}
	else cfgcpy.songLengthsFile = NULL;
	if(m_playerConfig.hvscDirectory != NULL)
	{
		cfgcpy.hvscDirectory = new char[strlen(m_playerConfig.hvscDirectory)+1];
		strcpy(cfgcpy.hvscDirectory,m_playerConfig.hvscDirectory);
	}
	else cfgcpy.hvscDirectory = NULL;

	return cfgcpy;
	*/
}

void CThreadSidPlayer::SetConfig(PlayerConfig* newConfig)
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

	numChann = (!m_inmod->config->GetBool(playbackConfigGroupGUID, L"mono", false) ?
				(m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1 : 1);

	// for the playback state to match correctly with the wacup core playback
	// config then we have to nudge everything otherwise stereo vs mono modes
	// may not be correctly applied at the time that it's expected to be done
	const SidConfig::playback_t playback = m_playerConfig.sidConfig.playback;
	m_playerConfig.sidConfig.playback = (SidConfig::playback_t)numChann;
	m_engine->config(m_playerConfig.sidConfig);
	m_playerConfig.sidConfig.playback = playback;


	//kernal,basic,chargen
	m_engine->setRoms(KERNAL_ROM, BASIC_ROM, CHARGEN_ROM);

	//create decode buf for 576 samples
	if(m_decodeBufLen > 0) delete[] m_decodeBuf;
	m_decodeBufLen = 2 * 576 * (PLAYBACK_BIT_PRECISION >>3) * numChann;
	m_decodeBuf = new char[m_decodeBufLen];
	//open song length database
	if((m_playerConfig.useSongLengthFile) && (m_playerConfig.songLengthsFile != NULL))
	{
		// TODO localise
		if(!m_sidDatabase.open(m_playerConfig.songLengthsFile))
			TimedMessageBox(NULL,L"Unable to open the songlength database which is needed for most files "
							L"to correctly report their duration (e.g. for correct looping).\r\n\nGoto "
							L"the SID preferences page where you can disable trying to use it or to "
							L"select an appropriate file to use instead.",L"in_sidious",MB_OK,3000);
	}
	//open STIL file
	if((m_playerConfig.useSTILfile) && (m_playerConfig.hvscDirectory != NULL))
	{
		ClearSTILData();
		FillSTILData();
		FillSTILData2();
	}
}

int CThreadSidPlayer::GetSongLengthMs(SidTune &tune)
{
	int length;

	if (tune.getStatus() == false)
	{
		//MessageBoxA(NULL, "Tune status invalid", "Error", MB_OK);
		return -1;
	}

	// attempt to use songlengths.md5 first before
	// attempting a songlengths.txt compatible set
	length = m_sidDatabase.lengthMs(tune);
	/*if (length <= 0)
	{
		length = m_sidDatabase.length(tune);
		if (length > 0)
		{
			length *= 1000;
		}
	}*/

	if ((m_playerConfig.playLimitEnabled) && (length <= 0))
	{
		length = (m_playerConfig.playLimitSec * 1000);
	}
	
	if (length < 0)
	{
		return 0;
	}
	return length;
}

int CThreadSidPlayer::GetSongLengthMs(void)
{
	return m_currentTuneLengthMs;
}

void CThreadSidPlayer::DoSeek()
{
	int bits;
	int skip_bytes;
	int numChn = (!m_inmod->config->GetBool(playbackConfigGroupGUID, L"mono", false) ?
				  (m_playerConfig.sidConfig.playback == SidConfig::STEREO)? 2 : 1 : 1);
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
		timesek = (int)((m_seekNeedMs - m_playTimems) / 1000);
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


void CThreadSidPlayer::SeekTo(int timeMs)
{
	m_seekNeedMs = timeMs;
}

void CThreadSidPlayer::FillSTILData()
{
	const int BUFLEN = 160;
	string strKey;
	string strInfo;
	char buf[BUFLEN];

	m.clear();
	FILE *f;
	strcpy(buf, m_playerConfig.hvscDirectory);
	strcat(buf, "\\documents\\stil.txt");
	f = fopen(buf, "rb+");
	if (f == NULL)
	{
		// TODO localise
		TimedMessageBox(NULL, L"Unable to open the STIL (SID Tune Information List) file.\r\n\nGoto the SID "
						L"preferences page where you can disable trying to use it or to select an appropriate "
						L"HVSC (High Voltage SID Collection) folder to use instead.",L"in_sidious",MB_OK,3000);
		return;
	}
	while (feof(f) == 0)
	{
		ReadLine(buf, f, 160);
		strKey.clear();
		strInfo.clear();
		if (buf[0] == '/') //new file block
		{
			strKey.assign(buf);
			FixPath(strKey);//.replace("/","\\");
			ReadLine(buf, f, BUFLEN);
			while (strlen(buf) > 0)
			{
				strInfo.append(buf);
				strInfo.append("\r\n");
				ReadLine(buf, f, BUFLEN);
			}
			m[_strdup(strKey.c_str())] = _strdup(strInfo.c_str());
		}
	}
	fclose(f);
}

void CThreadSidPlayer::FillSTILData2()
{
	const int BUFLEN = 160;
	const char* ARTIST =	" ARTIST:";
	const char* TITLE =		"  TITLE:";
	const char* COMMENT =	"COMMENT:";
	const char* AUTHOR = " AUTHOR:";
	const char* NAME = "   NAME:";
	string strKey;
	//string strInfo;
	string tmpStr;
	char buf[BUFLEN];
	int currentSubsong;
	StilBlock* stillBlock;
	vector<StilBlock*> subsongsInfo;

	m_stillMap2.clear();
	FILE *f;
	strcpy(buf, m_playerConfig.hvscDirectory);
	strcat(buf, "\\documents\\stil.txt");
	f = fopen(buf, "rb+");
	if (f == NULL)
	{
		// TODO localise
		TimedMessageBox(NULL, L"Unable to open the STIL (SID Tune Information List) file.\r\n\nGoto the SID "
						L"preferences page where you can disable trying to use it or to select an appropriate "
						L"HVSC (High Voltage SID Collection) folder to use instead.",L"in_sidious",MB_OK,3000);
		return;
	}
	while (feof(f) == 0)
	{
		ReadLine(buf, f, 160);
		strKey.clear();
		if (buf[0] == '/') //new file block
		{
			strKey.assign(buf);
			FixPath(strKey);//.replace("/","\\");
			currentSubsong = 0;

			ReadLine(buf, f, BUFLEN);
			stillBlock = new StilBlock;
			subsongsInfo = m_stillMap2[_strdup(strKey.c_str())];
			subsongsInfo.push_back(NULL);
			while (strlen(buf) > 0)
			{
				tmpStr.assign(buf);
				//check for subsong numer
				if (tmpStr.compare(0, 2, "(#") == 0)
				{
					int newSubsong = AStr2I(tmpStr.substr(2, tmpStr.length() - 3).c_str());
					//if subsong number is different than 1 then store current info and set subsong number to new value
					if (newSubsong != 1)
					{
						//store current subsong info
						subsongsInfo[currentSubsong] = stillBlock;
						currentSubsong = newSubsong-1;
						//ajust vetor size to number of subsongs
						while ((int)subsongsInfo.size() <= newSubsong)
						{
							subsongsInfo.push_back(NULL);
						}
						stillBlock = new StilBlock;
					}
				}
				//ARTIST
				if (tmpStr.compare(0, strlen(ARTIST), ARTIST) == 0)
				{
					stillBlock->ARTIST = tmpStr.substr(strlen(ARTIST) + 1);
				}
				//TITLE
				if (tmpStr.compare(0, strlen(TITLE), TITLE) == 0)
				{
					if (stillBlock->TITLE.empty())
					{
						stillBlock->TITLE = tmpStr.substr(strlen(TITLE) + 1);
					}
					else
					{
						stillBlock->TITLE.append(",");
						stillBlock->TITLE.append(tmpStr.substr(strlen(TITLE) + 1));
					}
				}
				//AUTHOR
				if (tmpStr.compare(0, strlen(AUTHOR), AUTHOR) == 0)
				{
					stillBlock->AUTHOR = tmpStr.substr(strlen(AUTHOR) + 1);
				}
				//NAME
				if (tmpStr.compare(0, strlen(NAME), NAME) == 0)
				{
					stillBlock->NAME = tmpStr.substr(strlen(NAME) + 1);
				}
				//COMMENT
				if (tmpStr.compare(0, strlen(COMMENT), COMMENT) == 0)
				{
					stillBlock->COMMENT = tmpStr.substr(strlen(COMMENT) + 1);
				}
				ReadLine(buf, f, BUFLEN);
			}
			subsongsInfo[currentSubsong] = stillBlock;

			m_stillMap2[_strdup(strKey.c_str())] = subsongsInfo;
		}
	}
	fclose(f);
}

void CThreadSidPlayer::FixPath(string& path)
{
	size_t i;
	for(i=0; i<path.length();++i)
	{
		if(path[i] == '/') path[i] = '\\';
	}
}

const char* CThreadSidPlayer::GetSTILData(const char* filePath)
{
	map<const char*,char*,ltstr>::iterator i;
	char* stilFileName;

	if((filePath == NULL)||(m_playerConfig.hvscDirectory == NULL)) return NULL;
	if(strlen(filePath) < strlen(m_playerConfig.hvscDirectory)) return NULL;
	stilFileName = new char[strlen(filePath) - strlen(m_playerConfig.hvscDirectory) +1];
	strcpy(stilFileName,&filePath[strlen(m_playerConfig.hvscDirectory)]);
	//i = m.find("aa\\DEMOS\\A-F\\Afterburner.sid");
	i = m.find(stilFileName);
	delete[] stilFileName;
	if(i == m.end())
	{
		return NULL;
	}
	return i->second;
	//if(i == NULL) return;
}

const StilBlock* CThreadSidPlayer::GetSTILData2(const char* filePath, int subsong)
{
	map<const char*, vector<StilBlock*>, ltstr>::iterator i;
	char* stilFileName;

	if ((filePath == NULL) || (m_playerConfig.hvscDirectory == NULL)) return NULL;
	if (strlen(filePath) < strlen(m_playerConfig.hvscDirectory)) return NULL;
	stilFileName = new char[strlen(filePath) - strlen(m_playerConfig.hvscDirectory) + 1];
	strcpy(stilFileName, &filePath[strlen(m_playerConfig.hvscDirectory)]);
	//i = m.find("aa\\DEMOS\\A-F\\Afterburner.sid");
	i = m_stillMap2.find(stilFileName);
	delete[] stilFileName;
	if (i == m_stillMap2.end())
	{
		return NULL;
	}

	if (subsong < (int)i->second.size())
	{
		return i->second[subsong];
	}
	return NULL;
	//if(i == NULL) return;
}

void CThreadSidPlayer::ClearSTILData(void)
{
	map<const char*, char*,ltstr>::iterator it = m.begin();
	while(it != m.end())
	{
		//const char *x= it->first;
		//const char *y= it->second;
		delete[] it->first;
		delete[] it->second;
		++it;
	}
	m.clear();

	map<const char*, vector<StilBlock*>, ltstr>::iterator it2 = m_stillMap2.begin();
	while (it2 != m_stillMap2.end())
	{
		//const char *x = it2->first;
		vector<StilBlock*> y = it2->second;
		delete[] it2->first;
		for (vector<StilBlock*>::iterator it3 = y.begin(); it3 != y.end(); ++it3)
		{
			if ((*it3) != NULL)
			{
				delete *it3;
			}
		}
		y.clear();
		++it2;
	}
	m_stillMap2.clear();
}
