#include "stdafx.h"
#include "YUVTrans.h"
#include "AgoraVideoCapture.h"

#include <aclapi.h>

#include <Mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "AgoraManager.h"
extern AgoraManager*	pAgoraManager;

#include "libyuv.h"
#pragma comment(lib,"yuv.lib")

typedef enum
{
	I420, YUY2, RGB24, RGB32
} PMMediaType;

typedef struct
{
	DWORD cb;
	DWORD size;
	PMMediaType mt;
	DWORD index;
	DWORD width;
	DWORD height;
	DWORD mapsize;
	DWORD sourcetype;
	char extra[256];
	char sign[256];
	DWORD livecount;
	DWORD videocount;
} PM_VIDEO_SHM_HEADER;

#define ALIGN_TO(x,a) (((x)+(a-1)) & ~(a-1))
#define PM_VIDEO_SHM_HEADER_PADDED_SIZE ALIGN_TO(sizeof(PM_VIDEO_SHM_HEADER),1024)


static void initYUVBuffer(char* psrc, int width, int height, int color = 0)
{
	if (!psrc || width <= 0 || height <= 0)
		return;

	int len = width * height;

	char* y = psrc;
	char* u = y + len;
	char* v = u + len / 4;

	char value = 16;
	for (int i = 0; i < len; i++)
	{
		y[i] = value;
	}
	len /= 4;
	for (int i = 0; i < len; i++)
	{
		u[i] = 128;
		v[i] = 126;
	}
}

DWORD SetLowLabelToObjectA(HANDLE hObject)
{
	DWORD dwErr;
	static const BYTE pSacl[0x1c] = {
		0x02, 0x00, 0x1c, 0x00, 0x01, 0x00, 0x00, 0x00, 0x11, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00
	};

	dwErr = SetSecurityInfo(hObject, SE_KERNEL_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, (PACL)pSacl);

	return dwErr;
}

VideoCaptureManager::VideoCaptureManager()
{
	if (!this->openSharedMemory())
		return;
//	this->startUpdate();

	this->hStartEvent = CreateEventA(NULL, TRUE, FALSE, "V6Live_Published_E");
	this->hStopEvent = CreateEventA(NULL, TRUE, FALSE, "V6Live_Stopped_E");

	this->nFps = 15;
	this->nWidth = 320;
	this->nHeight = 240;
	this->pCaptrueVideoData = (unsigned char*)malloc(1920 * 1080 * 4);
	m_lpBufferYUVRotate = new uint8_t[1920 * 1080 * 4];
	ZeroMemory(m_lpBufferYUVRotate, 1920 * 1080 * 4);
	m_lpBufferYUVMirror = new uint8_t[1920 * 1080 * 4];
	ZeroMemory(m_lpBufferYUVMirror, 1920 * 1080 * 4);
}

VideoCaptureManager::~VideoCaptureManager()
{
	if (this->pCaptrueVideoData)
	{
		free(this->pCaptrueVideoData);
	}
	if (m_lpBufferYUVRotate) {

		delete[] m_lpBufferYUVRotate;
		m_lpBufferYUVRotate = nullptr;
	}
	if (m_lpBufferYUVMirror) {

		delete[] m_lpBufferYUVMirror;
		m_lpBufferYUVMirror = nullptr;
	}
//	this->stotUpdate();
	this->closeSharedMemory();
	if (this->hStartEvent)
	{
		CloseHandle(this->hStartEvent);
	}
	if (this->hStopEvent)
	{
		CloseHandle(this->hStopEvent);
	}
}

BOOL VideoCaptureManager::openSharedMemory()
{
	this->dwVideoMapSize = 10 * 1024 * 1024;
	this->hMapVideo = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, this->dwVideoMapSize, "V6Live_SHM_VP");
	if (this->hMapVideo)
	{
		SetLowLabelToObjectA(this->hMapVideo);
		this->pMapVideo = (LPBYTE)MapViewOfFile(this->hMapVideo, FILE_MAP_ALL_ACCESS, 0, 0, this->dwVideoMapSize);
		if (!this->pMapVideo)
		{
			CloseHandle(this->hMapVideo);
			this->hMapVideo = NULL;
			return FALSE;
		}
		writelog("6vcamera:%p", this->pMapVideo);
		memset(this->pMapVideo, 0, this->dwVideoMapSize);
	}
	else
		return FALSE;

	SetLowLabelToObjectA(this->hStartEvent);
	SetLowLabelToObjectA(this->hStopEvent);
	return TRUE;
}

BOOL VideoCaptureManager::closeSharedMemory()
{
	if (this->hMapVideo)
	{
		CloseHandle(this->hMapVideo);
		this->hMapVideo = NULL;
	}
	if (this->pMapVideo)
	{
		UnmapViewOfFile(this->pMapVideo);
		this->pMapVideo = NULL;
	}
	return TRUE;
}

void VideoCaptureManager::initCapture(int nwidth, int nheight, int nfps)
{
	this->nWidth = nwidth;
	this->nHeight = nheight;
	this->nFps = nfps;
}

BOOL VideoCaptureManager::initSharedMemory()
{
	auto pp = (PM_VIDEO_SHM_HEADER*) this->pMapVideo;
	pp->width = this->nWidth;
	pp->height = this->nHeight;
	pp->mapsize = this->dwVideoMapSize;

// 	pCaptrueVideoData = new uint8_t[pp->width * pp->height * 4];
// 	ZeroMemory(pCaptrueVideoData, pp->width * pp->height * 4);
// 	m_lpBufferYUVRotate = new uint8_t[pp->width * pp->height * 4];
// 	ZeroMemory(m_lpBufferYUVRotate, pp->width * pp->height * 4);
// 	m_lpBufferYUVMirror = new uint8_t[pp->width * pp->height * 4];
// 	ZeroMemory(m_lpBufferYUVMirror, pp->width * pp->height * 4);

	if (!this->pCaptrueVideoData)
	{
		return FALSE;
	}
	ResetEvent(this->hStopEvent);
	ResetEvent(this->hStartEvent);
	SetEvent(this->hStartEvent);

	return TRUE;
}

void VideoCaptureManager::uninitSharedMemory()
{
	SetEvent(this->hStopEvent);
	ResetEvent(this->hStartEvent);
	ResetEvent(this->hStopEvent);
// 	if (this->pCaptrueVideoData)
// 	{
// 		delete[] this->pCaptrueVideoData;
// 		this->pCaptrueVideoData = NULL;
// 	}

// 	if (m_lpBufferYUVRotate){
// 
// 		delete[] m_lpBufferYUVRotate;
// 		m_lpBufferYUVRotate = nullptr;
// 	}
// 	if (m_lpBufferYUVMirror){
// 
// 		delete[] m_lpBufferYUVMirror;
// 		m_lpBufferYUVMirror = nullptr;
// 	}
}

BOOL VideoCaptureManager::startCapture()
{
// 	if (!this->initSharedMemory())
// 		return FALSE;
	this->bVideoCapture = TRUE;
	this->hVideoFlush = CreateThread(NULL, 0, VideoCaptureManager::VideoBufferFlashThread, this, 0, 0);
	if (!this->hVideoFlush)
		return FALSE;
	return TRUE;	
}

void VideoCaptureManager::stopCapture()
{
	this->bVideoCapture = FALSE;
	WaitForSingleObject(this->hVideoFlush, INFINITE);
	CloseHandle(this->hVideoFlush);
	this->hVideoFlush = NULL;
	//this->uninitSharedMemory();
}

//  BOOL WriteBMP(char* inPic, int width, int height, int bitsCount, TCHAR* strFileName)
//  {
//  	int DataSize = width * height * bitsCount / 8;
//  	HANDLE hf = CreateFile(strFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, NULL, NULL);
//  	//写文件头
//  	BITMAPFILEHEADER bfh;
//  	memset(&bfh, 0, sizeof(bfh));  //内存块置0
//  	bfh.bfType = 'MB';
//  	bfh.bfSize = sizeof(bfh) + DataSize + sizeof(BITMAPINFOHEADER);
//  	bfh.bfOffBits = sizeof(BITMAPINFOHEADER) + sizeof(BITMAPFILEHEADER);
//  	DWORD dwWritten = 0;
//  	WriteFile(hf, &bfh, sizeof(bfh), &dwWritten, NULL);
//  
//  	//写位图格式
//  	BITMAPINFOHEADER bin;
//  	memset(&bin, 0, sizeof(bin));  //内存块置0
//  	bin.biSize = sizeof(bin);
//  	bin.biWidth = width;
//  	bin.biHeight = height;
//  	bin.biPlanes = 1;
//  	bin.biBitCount = bitsCount;
//  	WriteFile(hf, &bin, sizeof(bin), &dwWritten, NULL);
//  
//  	//写位图数据
//  	WriteFile(hf, inPic, DataSize, &dwWritten, NULL);
//  	CloseHandle(hf);
// 	return TRUE;
//  }

// static int CutHalfPicture(unsigned char* pDst, unsigned char* pSrc2, int srcWidth, int srcHeight)
// {
// 	if (pDst == NULL || pSrc2 == NULL)
// 		return -1;
// 	if (srcWidth <= 0 || srcHeight <= 0)
// 		return -2;
// 	int dstWidth = srcWidth / 2;
// 	int dstHeight = srcHeight;
// 
// 	int dstHalfWidth = dstWidth / 2;
// 	int dstHalfHeight = dstHeight / 2;
// 
// 	int srcHalfWidth = srcWidth / 2;
// 	int srcHalfHeight = srcHeight / 2;
// 
// 	unsigned char * pDstY = pDst;
// 	unsigned char * pDstU = pDstY + dstWidth * dstHeight;
// 	unsigned char * pDstV = pDstU + dstHalfWidth * dstHalfHeight;
// 
// 	unsigned char * pSrcY2 = pSrc2;
// 	unsigned char * pSrcU2 = pSrcY2 + srcHeight * srcWidth;
// 	unsigned char * pSrcV2 = pSrcU2 + srcHalfWidth * srcHalfHeight;
// 
// 	int newWid = srcHalfWidth;
// 	int dstOffset = 0, srcOffset2 = srcHalfWidth / 2;
// 	for (int i = 0; i < srcHeight; i++)
// 	{
// 		memcpy(pDstY + dstOffset, pSrc2 + srcOffset2, newWid);
// 		dstOffset += dstWidth;
// 		srcOffset2 += srcWidth;
// 	}
// 
// 	dstOffset = 0;
// 	srcOffset2 = srcHalfWidth / 4;
// 	int newWid2 = srcHalfWidth / 2;
// 	for (int i = 0; i < dstHalfHeight; i++)
// 	{
// 		memcpy(pDstU + dstOffset, pSrcU2 + srcOffset2, newWid2);
// 		memcpy(pDstV + dstOffset, pSrcV2 + srcOffset2, newWid2);
// 		dstOffset += dstHalfWidth;
// 		srcOffset2 += srcHalfWidth;
// 	}
// 
// 	return 0;
// }
// 

Gdiplus::Image* LoadImageFromResource(UINT pSourceId, LPCTSTR pSourceType)
{
	HBITMAP hbitmap = nullptr;
	LPCTSTR pSourceName = MAKEINTRESOURCE(pSourceId);

	HMODULE hInstance = GetModuleHandle(0);

	HRSRC hResource = FindResource(hInstance, pSourceName, pSourceType);
	if (!hResource)
		return NULL;

	DWORD dResourceSize = SizeofResource(hInstance, hResource);
	if (!dResourceSize)
		return NULL;

	const void* pResourceData = LockResource(LoadResource(hInstance, hResource));
	if (!pResourceData)
		return NULL;

	HGLOBAL hResourceBuffer = GlobalAlloc(GMEM_MOVEABLE, dResourceSize);
	if (!hResourceBuffer)
	{
		GlobalFree(hResourceBuffer);
		return NULL;
	}

	void* pResourceBuffer = GlobalLock(hResourceBuffer);
	if (!pResourceBuffer)
	{
		GlobalUnlock(hResourceBuffer);
		GlobalFree(hResourceBuffer);
		return NULL;
	}

	CopyMemory(pResourceBuffer, pResourceData, dResourceSize);
	GlobalUnlock(hResourceBuffer);
	IStream* pIStream = NULL;

	if (CreateStreamOnHGlobal(hResourceBuffer, FALSE, &pIStream) == S_OK)
	{
		Gdiplus::Image * pImage = Gdiplus::Image::FromStream(pIStream);
		pIStream->Release();
		GlobalFree(hResourceBuffer);
		return pImage;
	}
	GlobalFree(hResourceBuffer);
	return NULL;
}


DWORD VideoCaptureManager::VideoBufferFlashThread(LPVOID lparam)
{
	VideoCaptureManager* pThis = (VideoCaptureManager*)lparam;

	if (!pThis->initSharedMemory())
	{
		writelog("init shared memory error");
		return FALSE;
	}
	Graphics* g = NULL;
	Bitmap* bitmap = NULL;
	bitmap = new Bitmap(pThis->nWidth, pThis->nHeight, PixelFormat32bppARGB);
	uint32_t nPicSize = pThis->nWidth * pThis->nHeight * 4;

	pThis->pLogo = LoadImageFromResource(IDB_PNG1, TEXT("PNG"));
	pThis->pLogo->RotateFlip(RotateFlipType::Rotate180FlipX);
	LPBYTE pCaptBuffer = (LPBYTE)malloc(nPicSize);

	initYUVBuffer((char*)pAgoraManager->m_CapVideoFrameObserver->m_lpImageBuffer, pThis->nWidth, pThis->nHeight);
	int nTimerTick = 1000 / pThis->nFps;
	pThis->hTimerEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	DWORD id = timeSetEvent(nTimerTick, 0, (LPTIMECALLBACK)pThis->hTimerEvent, 0, TIME_PERIODIC | TIME_CALLBACK_EVENT_SET);
	while (pThis->bVideoCapture)
	{
		DWORD ret = WaitForSingleObject(pThis->hTimerEvent, INFINITE);
		if (ret == WAIT_OBJECT_0) ResetEvent(pThis->hTimerEvent);
		else continue;

		if (!pCaptBuffer)
			pCaptBuffer = (LPBYTE)malloc(nPicSize);
		
		DWORD ndatasize = 0;
		LPBYTE pvideodata = NULL;
		PMMediaType ntype;
		PM_VIDEO_SHM_HEADER* ph = (PM_VIDEO_SHM_HEADER*)pThis->pMapVideo;
		if (ph->width != pThis->nWidth || ph->height != pThis->nHeight || !pCaptBuffer)
		{
			writelog("memory1[%d][%d] c[%d][%d] [%p][%p]",ph->width, ph->height, pThis->nWidth, pThis->nHeight, pCaptBuffer, pThis->pMapVideo);
			continue;
		}
		ndatasize = ph->size;
		ntype = ph->mt;
		ph->livecount = GetTickCount();
		pvideodata = pThis->pMapVideo + PM_VIDEO_SHM_HEADER_PADDED_SIZE;
		memcpy(pCaptBuffer, pvideodata, ndatasize);
		DWORD len = PM_VIDEO_SHM_HEADER_PADDED_SIZE;
		if (ndatasize >= (pThis->dwVideoMapSize - PM_VIDEO_SHM_HEADER_PADDED_SIZE))
		{
			ndatasize = 0;
		}
		if (ndatasize > 0)
		{
			switch (ntype)
			{
			case RGB32:
				if (pAgoraManager->ChatRoomInfo.bWaterMark)
				{
					BitmapData bitmapData;
					bitmap->LockBits(NULL, 0, PixelFormat32bppARGB, &bitmapData);
					if (pvideodata)
						memcpy(bitmapData.Scan0, pvideodata, nPicSize);
					bitmap->UnlockBits(&bitmapData);
					//bitmap->RotateFlip(RotateFlipType::Rotate180FlipX);
					g = Graphics::FromImage(bitmap);
					if (pThis->nWidth == 480)
						g->DrawImage(pThis->pLogo, pThis->nWidth / 4 * 3 - 42, pThis->nHeight - 24, 40, 22);
					else if (pThis->nWidth == 640)
						g->DrawImage(pThis->pLogo, pThis->nWidth / 4 * 3 - 56, pThis->nHeight - 32, 54, 30);
					else if (pThis->nWidth == 800)
						g->DrawImage(pThis->pLogo, pThis->nWidth / 4 * 3 - 68, pThis->nHeight - 38, 66, 36);
					else
						g->DrawImage(pThis->pLogo, pThis->nWidth / 4 * 3 - 42, pThis->nHeight - 24, 40, 22);

					bitmap->LockBits(NULL, 0, PixelFormat32bppARGB, &bitmapData);
					memcpy(pCaptBuffer, bitmapData.Scan0, nPicSize);
					bitmap->UnlockBits(&bitmapData);
				}
				//pThis->FormatTrans.RGB32ToI420(pCaptBuffer, pThis->pCaptrueVideoData, nPicSize, pThis->nWidth, pThis->nHeight);
				{
					//LibYUV rotate/trans
					int nWidth = pThis->nWidth; int nHeight = pThis->nHeight;
					unsigned char* src_frame = (uint8_t*)pCaptBuffer;
					unsigned char* pBuffer_dst_y = (uint8_t*)(pThis->pCaptrueVideoData);
					int ndst_stride_y = nWidth;
					unsigned char* pBuffer_dst_u = pBuffer_dst_y + nWidth * nHeight;
					int ndst_stride_u = nWidth / 2;
					unsigned char* pBuffer_dst_v = pBuffer_dst_u +  nWidth * nHeight / 4;
					int ndst_stride_v = nWidth / 2;

					uint8_t* pBuffer_dst_y_rotate180 = pThis->m_lpBufferYUVRotate;
					uint8_t* pBuffer_dst_u_rotate180 = pThis->m_lpBufferYUVRotate + nWidth * nHeight;
					uint8_t* pBuffer_dst_v_rotate180 = pThis->m_lpBufferYUVRotate + nWidth * nHeight + nWidth * nHeight / 4;

					uint8_t* pBuffer_dst_y_MirrorHorizion = pThis->m_lpBufferYUVMirror;
					uint8_t* pBuffer_dst_u_MirrorHorizion = pThis->m_lpBufferYUVMirror + nWidth * nHeight;
					uint8_t* pBuffer_dst_v_MirrorHorizion = pThis->m_lpBufferYUVMirror + nWidth * nHeight + nWidth * nHeight / 4;

					libyuv::ARGBToI420((unsigned char*)src_frame, nWidth * 4, pBuffer_dst_y, ndst_stride_y, pBuffer_dst_u, ndst_stride_u, pBuffer_dst_v, ndst_stride_v, nWidth, nHeight);
					libyuv::I420Rotate(pBuffer_dst_y, ndst_stride_y, pBuffer_dst_u, ndst_stride_u, pBuffer_dst_v, ndst_stride_v, pBuffer_dst_y_rotate180, ndst_stride_y, pBuffer_dst_u_rotate180, ndst_stride_u, pBuffer_dst_v_rotate180, ndst_stride_v,
						nWidth, nHeight, libyuv::RotationMode::kRotate180);
					libyuv::I420ToI420Mirror(pBuffer_dst_y_rotate180, ndst_stride_y, pBuffer_dst_u_rotate180, ndst_stride_u, pBuffer_dst_v_rotate180, ndst_stride_v,
						pBuffer_dst_y_MirrorHorizion, ndst_stride_y, pBuffer_dst_u_MirrorHorizion, ndst_stride_u, pBuffer_dst_v_MirrorHorizion, ndst_stride_v, nWidth, nHeight);
				}
				break;
// 			case RGB24:
// 				pThis->FormatTrans.RGB24ToI420(pvideodata, pThis->pCaptrueVideoData, nPicSize, pThis->nWidth, pThis->nHeight);
// 				break;
// 			case YUY2:
// 				pThis->FormatTrans.YUY2ToI420(pvideodata, pThis->pCaptrueVideoData, nPicSize, pThis->nWidth, pThis->nHeight);
// 				break;
// 			case I420:
// 				memcpy(pThis->pCaptrueVideoData, pvideodata, ndatasize);
// 				break;
			default:
				writelog("error type:%d", ntype);
				break;
			}

			if (pAgoraManager->m_CapVideoFrameObserver->m_lpImageBuffer && pThis->m_lpBufferYUVMirror)
				memcpy(pAgoraManager->m_CapVideoFrameObserver->m_lpImageBuffer, pThis->m_lpBufferYUVMirror, pThis->nWidth* pThis->nHeight * 3 / 2);

			if (ph->width != pThis->nWidth || ph->height != pThis->nHeight || !pCaptBuffer)
			{
				writelog("memory2[%d][%d] c[%d][%d] [%p][%p]", ph->width, ph->height, pThis->nWidth, pThis->nHeight, pCaptBuffer, pThis->pMapVideo);
			}
		}
	}
	if (bitmap)
	{
		delete(bitmap);
		bitmap = NULL;
	}
	if (pCaptBuffer)
	{
		free(pCaptBuffer);
		pCaptBuffer = NULL;
	}
	if (pThis->pLogo)
	{
		DeleteObject(pThis->pLogo);
		pThis->pLogo = NULL;
	}
	timeKillEvent(id);
	if (pThis->hTimerEvent)
	{
		CloseHandle(pThis->hTimerEvent);
		pThis->hTimerEvent = NULL;
	}
	pThis->uninitSharedMemory();
	return 0;
}

BOOL VideoCaptureManager::startUpdate()
{
	this->bLiveCount = TRUE;
//	this->hLiveCount = CreateThread(NULL, 0, VideoCaptureManager::UpdateLivecount, this, 0, 0);
	if (!this->hLiveCount)
	{
		return FALSE;
	}
	return TRUE;
}

BOOL VideoCaptureManager::stotUpdate()
{
	return TRUE;
}
