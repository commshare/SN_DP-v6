#include "ConvertPCM.h"
#include "Utils.h"
#include "HookAudioInput.h"
#include "PlayerHooker.h"

const DWORD dwDEFAULT_BLOCK_COUNT = 16;
const DWORD dwCAPTURE_BUFFER_SIZE = dwNOTIFY_SIZE * dwDEFAULT_BLOCK_COUNT;
const DWORD dwGET_DATA_WAIT_TIME = 200;

bool HaveHookAudioRunning()
{
	CSharedMem sharedMem(pszSHARE_MAP_FILE_NAME, dwSHARE_MEM_SIZE);
	int hookCount = sharedMem.GetDwordValue(pszHOOK_PROCESS_START_SECTION_NAME, 0);
	CAudioDataHooker::ms_log.Trace(_T("HaveHookAudioRunning: START_SECTION_NAME %d\n"),hookCount);
	return hookCount% 2 == 1;
}

// CHookAudioInput

CHookAudioInput::CHookAudioInput():
	CThread(true),  m_sleep(20), m_sharedMem(pszSHARE_MAP_FILE_NAME, dwSHARE_MEM_SIZE),
		m_pResampler(NULL), m_stop(true)
{
	m_pNotifyBuffer = (char*)malloc(dwNOTIFY_SIZE * 2);
	m_hookRef = m_sharedMem.GetDwordValue(pszHOOK_PROCESS_START_SECTION_NAME, 0);
	mpCallback = NULL;
	
	TCHAR szBuf[MAX_PATH] = { _T("\0") };
	ZeroMemory(szBuf, MAX_PATH);
	GetModuleFileName(NULL, szBuf, MAX_PATH);
	LPTSTR lpLastSlash = _tcsrchr(szBuf, _T('\\'));
	TCHAR tchPcmPath[MAX_PATH] = { _T("\0") };
	ZeroMemory(tchPcmPath,MAX_PATH);
	memcpy(tchPcmPath, szBuf, sizeof(TCHAR) * (lpLastSlash - szBuf));
	_tcscat(tchPcmPath,_T("\\V6room\\HookDest.pcm"));
	CAudioDataHooker::ms_log.Trace(_T("AudioIniput Notify PcmPath: %s\n"),tchPcmPath);
	m_strAudioPcmPath = tchPcmPath;
	DeleteFile(CString(tchPcmPath));

	Resume();
}

CHookAudioInput::~CHookAudioInput()
{
	Quit();
	//to make sure thread quit
	m_sharedMem.SetValue(pszHOOK_PROCESS_AUDIO_DATA_SECTION_NAME, m_pNotifyBuffer, 1);
	if (m_pNotifyBuffer != NULL)
	{
		free(m_pNotifyBuffer);
	}
	if (m_pResampler != NULL)
	{
		m_pResampler->Destroy();
	}

	if (mpCallback){//Notify Exit
		mpCallback->onCaptureStop();
		mpCallback = nullptr;
	}
}

const DWORD dwWAIT_NOTIFY_SIZE = dwNOTIFY_SIZE * 4;

void CHookAudioInput::Execute()
{
	SetPriority(THREAD_PRIORITY_TIME_CRITICAL);

	while (!Terminated)
	{
		UINT bytesRead;
		DWORD hookDataLen;
		DWORD hookCommand;
		bool canReadHookData;
		bool haveIntallHook = m_sharedMem.GetDwordValue(pszHOOK_PROCESS_INSTALL_COUNT_SECTION_NAME, 0) > 0;

		canReadHookData = haveIntallHook
			&& (m_sharedMem.GetDwordValue(pszHOOK_PROCESS_START_SECTION_NAME, 0) == m_hookRef);
		char szLog[1024] = {'\0'};
		sprintf_s(szLog, "canReadHookData: %d %d \n", canReadHookData,GetTickCount());
		OutputDebugStringA(szLog);

		if (canReadHookData)
		{
			m_sharedMem.SetDwordValue(pszHOOK_PROCESS_COMMAND_SECTION_NAME, dwHOOK_AUDIO_DATA_NEED);
			//OutputDebugStringA("pszHOOK_PROCESS_COMMAND_SECTION_NAME dwHOOK_AUDIO_DATA_NEED\r\n");
			if (m_sharedMem.WaitForValueChange(pszHOOK_PROCESS_AUDIO_DATA_SECTION_NAME, dwGET_DATA_WAIT_TIME)
				== WAIT_TIMEOUT)
			{
				TCHAR buf[255];
				DWORD valueSize = 255;
				memset(buf, 0, sizeof(buf));
				if (m_sharedMem.GetValue(pszHOOK_PROCESS_PATH_SECTION_NAME, buf, &valueSize))
				{
					if (!IsProcessRunning(buf))
					{
						DWORD installCount = m_sharedMem.GetDwordValue(pszHOOK_PROCESS_INSTALL_COUNT_SECTION_NAME, 0);
						if (installCount > 0)
						{
							installCount--;
						}
						m_sharedMem.SetDwordValue(pszHOOK_PROCESS_INSTALL_COUNT_SECTION_NAME, installCount);
					}
				}

				continue;
			}
		}

		if (Terminated)
		{
			break;
		}
		
		m_hookChunk.Reset();

		if (canReadHookData)
		{
			hookCommand = m_sharedMem.GetDwordValue(pszHOOK_PROCESS_COMMAND_SECTION_NAME, dwHOOK_AUDIO_DATA_WAIT);
			if (hookCommand != dwHOOK_AUDIO_DATA_EMPTY )
			{
				m_onceHaveHookData = true;
				m_sharedMem.SetDwordValue(pszHOOK_PROCESS_COMMAND_SECTION_NAME, dwHOOK_AUDIO_DATA_WAIT);
				//OutputDebugStringA("pszHOOK_PROCESS_COMMAND_SECTION_NAME dwHOOK_AUDIO_DATA_WAIT\r\n");
				if (m_sharedMem.GetValue(pszHOOK_PROCESS_AUDIO_DATA_SECTION_NAME, m_pNotifyBuffer, &hookDataLen))
				{
					//CAudioDataHooker::ms_log.Trace(_T("Audio_DATA_SECTION_NAME: %d\n"),hookDataLen);
					if (isSaveDumpPcm){

						//FILE* outfile = fopen("D:\\V6room\\HookDest.pcm", "ab+");
						FILE* outfile = fopen(CStringA(m_strAudioPcmPath.data()), "ab+");
						if (outfile)
						{
							fwrite(m_pNotifyBuffer, 1, hookDataLen, outfile);
							fclose(outfile);
							outfile = NULL;
						}
					}

					static int nCountAudioNotify = 0;
					nCountAudioNotify++;
					static DWORD dwTimeNotifyStamp = GetTickCount();
					if (5000 < GetTickCount() - dwTimeNotifyStamp){
						float fAudioNotifyRate = nCountAudioNotify * 1000.0 / (GetTickCount() - dwTimeNotifyStamp);

						CAudioDataHooker::ms_log.Trace(_T("HookAudioInput NotifyCaptureData: %d,%d,%d,%d [ NotifyRate: %.2f]\n"),
							dwHOOK_AUDIO_DATA_CHANNEL,32,dwHOOK_AUDIO_DATA_SAMPLERATE,hookDataLen,fAudioNotifyRate);
						nCountAudioNotify = 0;
						dwTimeNotifyStamp = GetTickCount();
					}

					m_hookChunk.SetData(m_pNotifyBuffer, hookDataLen, dwHOOK_AUDIO_DATA_SAMPLERATE,
						dwHOOK_AUDIO_DATA_CHANNEL, 32, true);
					NotifyCaptureData();
					char szLog2[256] = { '\0' };
					sprintf_s(szLog, "NotifyCaptureData: : %d %d \n", hookDataLen, GetTickCount());
					OutputDebugStringA(szLog);
				}
			}
			else{
				static int nCount = 0;
				static int lasttime = GetTickCount();
				nCount++;

				if (5000 < GetTickCount() - lasttime){
					char szbuf[128] = { '\0' };
					sprintf_s(szbuf, "DATA_EMPTY Rate: %d\r\n", nCount);
					//OutputDebugStringA(szbuf);
					float fAudioEmptyRate = nCount * 1000.0 / (GetTickCount() - lasttime);
					CAudioDataHooker::ms_log.Trace(_T("HookAudioInput DATA_EMPTY Rate: %.2f"),fAudioEmptyRate);
					lasttime = GetTickCount();
					nCount = 0;
				} 
			}
		}
		
		m_sleep.Wait();
	}
}

void CHookAudioInput::NotifyCaptureData()
{
	INSYNC(m_lock);
	if (mpCallback != NULL)
	{		

		{
			if (m_pResampler == NULL && m_formatEx.nSamplesPerSec != dwHOOK_AUDIO_DATA_SAMPLERATE)
			{
				m_pResampler = CreateResampler();
				m_pResampler->Init(m_formatEx.nChannels, dwHOOK_AUDIO_DATA_SAMPLERATE, m_formatEx.nSamplesPerSec);
			}

			if (m_pResampler != NULL)
			{
				if (m_formatEx.nChannels == 1 && m_hookChunk.GetChannels() == 2)
				{
					ConvertStereoChunkToMono(&m_hookChunk);
				}
				CAudioChunk outChunk;
				m_pResampler->Process(&m_hookChunk, &outChunk);
				int outSize = ConvertFloatTo16Bit((char*)outChunk.GetData(), outChunk.GetDataSize());
				m_notifyData.append((char*)outChunk.GetData(), outSize);
			}
			else
			{
				int outSize = ConvertFloatTo16Bit((char*)m_hookChunk.GetData(), m_hookChunk.GetDataSize());
				m_notifyData.append((char*)m_hookChunk.GetData(), outSize);
			}
		}

		if (m_notifyData.size() >= m_notifySize)
		{
			UINT totalNotifySize = 0;
			UINT notifyCount = (UINT)m_notifyData.size() / m_notifySize;
			for (UINT i = 0; i < notifyCount; i++)
			{
				if (!m_stop)
				{
					mpCallback->onCapturedData((char*)m_notifyData.c_str() + i * m_notifySize, m_notifySize, &m_formatEx);
					totalNotifySize += m_notifySize;
				}
				else
				{
					break;
				}
			}
			m_notifyData.erase(0, totalNotifySize);
		}					
	}
}

bool CHookAudioInput::Open(int sampleRate, int channels, int bps, int inputBufferSize, int notifySize)
{
	INSYNC(m_lock);
	m_notifySize = notifySize;
	m_formatEx.wFormatTag = 1;
	m_formatEx.nChannels = channels; 
	m_formatEx.nSamplesPerSec = sampleRate;    
	m_formatEx.nAvgBytesPerSec = (sampleRate * (bps / 8)) * channels;   
	m_formatEx.nBlockAlign = (bps / 8) * channels;       
	m_formatEx.wBitsPerSample = bps;    
	m_formatEx.cbSize = 0;  
	return true;
}

void CHookAudioInput::Close()
{
}

void CHookAudioInput::Start(IAudioCaptureCallback* callback)
{
	INSYNC(m_lock);
	m_stop = false;
	mpCallback = callback;
	if (mpCallback != NULL)
	{
		mpCallback->onCaptureStart();
	}
}

void CHookAudioInput::Stop()
{
	INSYNC(m_lock);
	if (m_stop)
	{
		return;
	}
	m_stop = true;
	DWORD dwHookCount = m_sharedMem.GetDwordValue(pszHOOK_PROCESS_INSTALL_COUNT_SECTION_NAME, 0);
	bool haveIntallHook = dwHookCount > 0;
	CAudioDataHooker::ms_log.Trace(_T("HookAudioInput Stop Capture INSTALL_COUNT_SECTION_NAME: %d\n"), dwHookCount);
	if (haveIntallHook)
	{
		CThread::Terminate();
		if (mpCallback != NULL)
		{
			mpCallback->onCaptureStop();
			mpCallback = NULL;
		}
	}
	else
	{
		m_notifyData.clear();
		if (m_pResampler != NULL)
		{
			m_pResampler->Destroy();
			m_pResampler = NULL;
		}
	}
}

bool CHookAudioInput::IsCapturing()
{
	return !m_stop;;
}