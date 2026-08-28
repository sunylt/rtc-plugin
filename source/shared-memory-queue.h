#pragma once
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <string>
#include <atomic>
#include "Tracer.h"
using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

struct video_queue;
struct nv12_scale;
typedef struct video_queue video_queue_t;
typedef struct nv12_scale nv12_scale_t;

enum queue_state {
	SHARED_QUEUE_STATE_INVALID,
	SHARED_QUEUE_STATE_STARTING,
	SHARED_QUEUE_STATE_READY,
	SHARED_QUEUE_STATE_STOPPING,
};

struct rtc_video_format {
	uint32_t width;
	uint32_t height;
	uint32_t size;
	uint32_t format;
	uint64_t timestamp;
	char *buffer;
	//int buffer_size;
	//int buffer_offer;
};

struct rtc_audio_format {
	uint32_t size;
	uint64_t timestamp;
	char buffer[1920];
};

//struct rtc_audio_format {
//	uint32_t size;
//	uint64_t timestamp;
//	char *buffer;
//};

// �����ڴ����ݽṹ���������ƿ�����ݻ�������
#pragma pack(push, 1)
struct SharedMemoryVideoData {
	volatile bool isReady;   // �����Ƿ����
	volatile bool isWriting; // �Ƿ�����д��
	uint32_t dataSize;           // ʵ�����ݴ�С
	uint32_t width;              // ��Ƶ����
	uint32_t height;             // ��Ƶ�߶�
	uint32_t format;             // ��Ƶ��ʽ
	uint64_t timestamp;          // ʱ���
    char videoData[3840 * 2160 * 4];// ���ݻ���������
};

struct SharedMemoryAudioData {
	volatile bool isReady;   // �����Ƿ����
	volatile bool isWriting; // �Ƿ�����д��
	uint32_t dataSize;           // ʵ�����ݴ�С
	uint64_t timestamp;          // ʱ���
	char audioData[1920 * 1080 * 4];  // ���ݻ���������
};
#pragma pack(pop)

enum DataType 
{ 
	AudioData = 1,
	VideoData 
};

//extern void copy_frame_data_plane2(uint8_t *dst, int line_size, const struct obs_source_frame *src, uint32_t plane, uint32_t lines);
//
//extern void copy_frame_data_line2(uint8_t *dst, int line_size, const struct obs_source_frame *src, uint32_t plane, uint32_t y);


class SharedMemory {
private:
	std::wstring name;      // �����ڴ�����
	HANDLE m_hMapFile;        // �����ڴ���
	SharedMemoryVideoData *m_videoData = nullptr; // �����ڴ�����ָ��
	SharedMemoryAudioData *m_audioData = nullptr;
	HANDLE m_hMutex = nullptr;          // ���������

public:
	// �����캯���������µĹ����ڴ棩
	explicit SharedMemory(const std::wstring &name, DataType type);

	// �����캯���������й����ڴ棩
	explicit SharedMemory(const std::wstring &name, DataType type, bool openExisting);

	SharedMemory(const SharedMemory&) = delete;
	SharedMemory& operator=(const SharedMemory&) = delete;
	SharedMemory(SharedMemory&&) = delete;
	SharedMemory& operator=(SharedMemory&&) = delete;

	// ��������
	~SharedMemory()
	{
		if (m_videoData) {
			UnmapViewOfFile(m_videoData);
		}
		if (m_audioData) {
			UnmapViewOfFile(m_audioData);
		}
		if (m_hMapFile) {
			CloseHandle(m_hMapFile);
		}
		if (m_hMutex) {
			CloseHandle(m_hMutex);
		}
	}

	// д������
	void writeVideoData(rtc_video_format videoData);
	void writeAudioData(rtc_audio_format audioData);

	// ��ȡ����
	bool readVideoDate(rtc_video_format &videoData);
	bool readAudioDate(rtc_audio_format &audioData);
	bool readData(char *buffer, uint32_t &size, uint32_t &width, uint32_t &height, uint32_t &format, uint64_t &timestamp)
	{
		if (m_videoData == nullptr || m_hMutex == nullptr) return false;
		// �ȴ�������
		const DWORD waitResult = WaitForSingleObject(m_hMutex, INFINITE);
		if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_ABANDONED) return false;

		// ��������Ƿ������δ��д��
		bool ready = m_videoData->isReady;
		MemoryBarrier();
		bool writing = m_videoData->isWriting;
		if (!ready || writing) {
			ReleaseMutex(m_hMutex);
			return false;
		}

		// ��������
		size = m_videoData->dataSize;
		width = m_videoData->width;
		height = m_videoData->height;
		format = m_videoData->format;
		timestamp = m_videoData->timestamp;

		if (size > sizeof(m_videoData->videoData)) {
			m_videoData->isReady = false;
			MemoryBarrier();
			ReleaseMutex(m_hMutex);
			return false;
		}
		if (buffer && size > 0) {
			memcpy(buffer, m_videoData->videoData, size);
		}

		// ��������Ѷ�ȡ
		m_videoData->isReady = false;
		MemoryBarrier();

		// �ͷŻ�����
		ReleaseMutex(m_hMutex);
		return true;
	}

	// ��������Ƿ����
	bool isDataReady(DataType type);

};
#ifdef __cplusplus
}
#endif
